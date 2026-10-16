/* 此片段只由sc32_debug.c纳入。登记是分阶段真实文件/映像核验，
 * 每次最多读取/比对64KiB，ROOT在PENDING之间让出；不信任应用声称
 * 的符号、库名或映射长度，也不进入目标的SCLDR私有链表。 */
static const u8 *d2_record(const d2_module_t *m,u32 field,u32 stride,u32 index)
{return (const u8 *)m->metadata+d2_word((const u8 *)m->metadata,field)+stride*index;
}
static const char *d2_string(const d2_module_t *m,u32 offset)
{return (const char *)m->metadata+d2_word((const u8 *)m->metadata,SCL_H_STRINGS)+offset;
}
static void d2_segment(d2_module_t *m,u32 index,ScDebugSegment *out)
{
    d2_zero(out,sizeof(*out));
    out->size=sizeof(*out);
    out->version=1;
    out->module=m->info.id;
    out->module_generation=m->info.generation;
    out->index=index;
    if(m->info.kind==SC32_DEBUG_MAIN){out->address=m->info.base;
        out->bytes=m->info.image_bytes;
        out->access=7;
    }else{
        const u8 *s=d2_record(m,SCL_H_SEGMENTS,SCL_SEGMENT_SIZE,index);
        out->address=m->info.base+d2_word(s,SCL_S_RVA);
        out->bytes=d2_word(s,SCL_S_MEMORY_SIZE);
        out->access=d2_word(s,SCL_S_FLAGS);
    }
    /* R/RW取现在真实页表；X是格式声明，非PAE并无NX，不能把它
     * 说成硬件禁止数据页执行。混合RW页明确置MIXED而不猜一个值。 */
    u32 pd=TASK((int)m->owner).pd,read_only=0,writable=0;
    if(!paging_user_range(pd,out->address,out->bytes)){out->access=0;
        return;
    }
    u32 *directory=(u32 *)pd;
    for(u32 at=out->address;at<out->address+out->bytes;){u32 *table=(u32 *)(directory[at>>22]&~4095u);
        if(table[(at>>12)&1023u]&2u)writable=1;
        else read_only=1;
        u32 step=4096u-(at&4095u);
        if(step>=out->address+out->bytes-at)break;
        at+=step;
    }
    out->access=(out->access&SC32_DEBUG_EXECUTABLE)|SC32_DEBUG_READABLE
    |(writable&&!read_only?SC32_DEBUG_WRITABLE:0)|(writable&&read_only?SC32_DEBUG_ACCESS_MIXED:0);
}
static void d2_symbol(d2_module_t *m,u32 index,ScDebugSymbol *out)
{
    const u8 *symbol=d2_record(m,SCL_H_EXPORTS,SCL_EXPORT_SIZE,index);
    d2_zero(out,sizeof(*out));
    out->size=sizeof(*out);
    out->version=1;
    out->module=m->info.id;
    out->module_generation=m->info.generation;
    out->index=index;
    out->address=m->info.base+d2_word(symbol,SCL_E_RVA);
    out->bytes=d2_word(symbol,SCL_E_SIZE);
    out->kind=d2_word(symbol,SCL_E_KIND);
    out->flags=SC32_DEBUG_SYMBOL_EXPORTED;
    d2_text(out->name,sizeof(out->name),d2_string(m,d2_word(symbol,SCL_E_NAME)));
}
static d2_module_t *d2_named(d2_state_t *state,const char *name)
{
    for(u32 id=state->modules;id;){d2_module_t *m=d2_module(id);
        if(!m)break;
        if(m->info.kind==SC32_DEBUG_SCL && d2_equal(m->info.name,name))return m;
        id=m->next;
    }
    return 0;
}
static int d2_relocation(d2_state_t *state,d2_module_t *m,const u8 *r,u32 *out)
{
    const u8 *file=(const u8 *)m->file;
    u32 target=d2_word(r,SCL_R_TARGET);
    if(d2_word(r,SCL_R_TARGET_KIND)==SCL_TARGET_INTERNAL)target+=m->info.base;
    else{
        const u8 *import=file+d2_word(file,SCL_H_IMPORTS)+target*SCL_IMPORT_SIZE;
        const u8 *dep=file+d2_word(file,SCL_H_DEPS)+d2_word(import,SCL_I_DEPENDENCY)*SCL_DEPENDENCY_SIZE;
        const char *strings=(const char *)file+d2_word(file,SCL_H_STRINGS);
        d2_module_t *dependency=d2_named(state,strings+d2_word(dep,SCL_D_NAME));
        if(!dependency || dependency->info.abi_major!=d2_word(dep,SCL_D_ABI_MAJOR)
        || dependency->info.abi_minor<d2_word(dep,SCL_D_MIN_MINOR)
        || dependency->info.library_version<d2_word(dep,SCL_D_MIN_VERSION))return SC32_E_NOT_FOUND;
        const char *name=strings+d2_word(import,SCL_I_NAME);
        int found=0;
        u32 low=0,count=dependency->info.symbol_count;
        while(count){u32 half=count/2,index=low+half;
            const u8 *symbol=d2_record(dependency,SCL_H_EXPORTS,SCL_EXPORT_SIZE,index);
            const char *current=d2_string(dependency,d2_word(symbol,SCL_E_NAME));
            const char *a=current,*b=name;
            while(*a && *a==*b){a++;
                b++;
            }int order=(u8)*a-(u8)*b;
            if(!order){if(d2_word(symbol,SCL_E_KIND)!=d2_word(import,SCL_I_KIND)
                || d2_word(symbol,SCL_E_SIZE)<d2_word(import,SCL_I_SIZE))return SC32_E_VERSION;
                target=dependency->info.base+d2_word(symbol,SCL_E_RVA);
                found=1;
                break;
            }
            if(order<0){low=index+1;
                count-=half+1;
            }else count=half;
        }
        if(!found)return SC32_E_NOT_FOUND;
    }
    u32 value=target+d2_word(r,SCL_R_ADDEND);
    if(d2_word(r,SCL_R_TYPE)==SCL_RELOC_PC32)value-=m->info.base+d2_word(r,SCL_R_PATCH);
     *out=value;
    return 0;
}
static int d2_mapping_access(u32 pd,u32 address,u32 bytes,u32 write)
{
    if(!paging_user_range(pd,address,bytes))return 0;
    u32 *directory=(u32 *)pd;
    u32 end=address+bytes;
    for(u32 at=address;at<end;){u32 de=directory[at>>22];
        u32 *table=(u32 *)(de&~4095u);
        u32 pte=table[(at>>12)&1023u];
        if(((pte&2u)!=0)!=(write!=0))return 0;
        u32 step=4096u-(at&4095u);
        if(step>end-at)break;
        at+=step;
    }
    return 1;
}
static int d2_module_begin(int pid,d2_state_t *state,const Sc32DebugRegistration *r,d2_module_t **out)
{
    if(!r->path || !r->path_bytes || r->path_bytes>64 || !r->metadata || r->metadata_bytes<128
    || r->metadata_bytes>SCL_MAX_FILE_SIZE || r->flags || r->module || r->module_generation
    || r->reserved[0] || r->reserved[1])return SC32_E_INVALID;
    char path[64],canonical[64];
    if(paging_user_copy(TASK(pid).pd,r->path,path,r->path_bytes,0)
    || path[r->path_bytes-1] || r->path_bytes<2)return SC32_E_INVALID;
    for(u32 i=0;i+1<r->path_bytes;i++)if(!path[i])return SC32_E_INVALID;
    if(fs_normalize(path,canonical)<=0)return SC32_E_INVALID;
    for(u32 id=state->modules;id;){d2_module_t *m=d2_module(id);
        if(!m)break;
        if(m->info.base==r->base)return SC32_E_EXISTS;
        id=m->next;
    }
    u32 allocation=paging_module_size(TASK(pid).pd,r->base);
    if(!allocation)return SC32_E_RANGE;
    u32 source=0;
    /* native接口即使只读也要求非零调用身份；任务代数在本次核验
     * 期间稳定。这里只打开既有只读源，不创建写事务；释放仍传0，
     * 因而不会因数值碰撞取消同一文件上其它句柄的真实pending。 */
    int error=fs_native_open(pid,task_generation(pid),canonical,FS_NATIVE_READ,0,&source);
    if(error)return error==FS_N_ACCESS?SC32_E_ACCESS:error==FS_N_NOT_FOUND?SC32_E_NOT_FOUND:SC32_E_IO;
    fs_native_info_t fi;
    error=fs_native_info(pid,source,FS_ACCESS_READ,&fi);
    if(error || fi.length<SCL_HEADER_SIZE || fi.length>SCL_MAX_FILE_SIZE){fs_native_close(source,0);
        return SC32_E_RANGE;
    }
    int index=objpool_alloc(&d2_modules);
    u32 serial=d2_next_serial();
    if(index<0 || !serial){if(index>=0)objpool_release(&d2_modules,index);
        fs_native_close(source,0);
        return SC32_E_MEMORY;
    }
    d2_module_t *m=objpool_get(&d2_modules,index);
    d2_zero(m,sizeof(*m));
    m->info.size=sizeof(m->info);
    m->info.version=1;
    m->info.id=(u32)index+1;
    m->info.generation=serial;
    m->owner=(u32)pid;
    m->owner_generation=task_generation(pid);
    m->source_object=source;
    m->info.kind=SC32_DEBUG_SCL;
    m->info.base=r->base;
    m->info.allocation_bytes=allocation;
    m->info.source_bytes=fi.length;
    m->info.source_revision=fi.revision;
    m->info.source[0]='/';
    d2_text(m->info.source+1,sizeof(m->info.source)-1,canonical);
    m->file_pages=(fi.length+4095u)/4096u;
    m->file=pframe_alloc_run(m->file_pages);
    if(!m->file){d2_module_drop(m);
        return SC32_E_MEMORY;
    }
    m->input_metadata=r->metadata;
    m->input_metadata_bytes=r->metadata_bytes;
    m->proof_mapping_epoch=state->mapping_epoch;
    m->proof_reloc_index=0xffffffffu;
    sha256_init(&m->hash);
    state->loading=m->info.id;
     *out=m;
    return 0;
}
static int d2_module_step(int pid,d2_state_t *state,d2_module_t *m)
{
    if(m->proof_mapping_epoch!=state->mapping_epoch)return SC32_E_STALE;
    fs_native_info_t fi;
    int error=fs_native_info(pid,m->source_object,FS_ACCESS_READ,&fi);
    if(error || fi.revision!=m->info.source_revision || fi.length!=m->info.source_bytes)return SC32_E_STALE;
    u8 *file=(u8 *)m->file;
    u32 budget=65536u;
    if(m->phase==0){u32 count=m->info.source_bytes-m->read_at;
        if(count>budget)count=budget;
        u32 done=0;
        error=fs_native_read(pid,m->source_object,file+m->read_at,count,m->read_at,&done);
        if(error || done!=count)return SC32_E_IO;
        sha256_update(&m->hash,file+m->read_at,count);
        m->read_at+=count;
        if(m->read_at!=m->info.source_bytes)return SC32_PENDING;
        if(scl_validate(file,m->info.source_bytes))return SC32_E_INVALID;
        u32 metadata_bytes=d2_word(file,SCL_H_STRINGS)+d2_word(file,SCL_H_STRINGS_SIZE);
        if(metadata_bytes!=m->input_metadata_bytes)return SC32_E_INVALID;
        u32 image=d2_word(file,SCL_H_IMAGE_SIZE);
        task_record_t *record=task_record(pid);
        if(m->info.allocation_bytes!=image && !(record->scl_root_base==m->info.base
        && record->scl_root_image_size==image && record->scl_root_bytes==m->info.allocation_bytes))return SC32_E_RANGE;
        const char *name=(const char *)file+d2_word(file,SCL_H_STRINGS)+d2_word(file,SCL_H_NAME);
        if(d2_named(state,name))return SC32_E_EXISTS;
        d2_text(m->info.name,sizeof(m->info.name),name);
        m->info.image_bytes=image;
        m->info.abi_major=d2_word(file,SCL_H_ABI_MAJOR);
        m->info.abi_minor=d2_word(file,SCL_H_ABI_MINOR);
        m->info.library_version=d2_word(file,SCL_H_LIBRARY_VERSION);
        m->info.segment_count=d2_word(file,SCL_H_SEGMENT_COUNT);
        m->info.symbol_count=d2_word(file,SCL_H_EXPORT_COUNT);
        m->metadata_pages=(metadata_bytes+4095u)/4096u;
        m->metadata=pframe_alloc_run(m->metadata_pages);
        if(!m->metadata)return SC32_E_MEMORY;
        sha256_final(&m->hash,m->info.sha256);
        m->phase=1;
        m->read_at=0;
        return SC32_PENDING;
    }
    if(m->phase==1){u8 chunk[256];
        u32 remaining=m->input_metadata_bytes-m->read_at;
        while(remaining && budget){u32 n=remaining;
            if(n>sizeof(chunk))n=sizeof(chunk);
            if(paging_user_copy(TASK(pid).pd,m->input_metadata+m->read_at,chunk,n,0))return SC32_E_INVALID;
            for(u32 i=0;i<n;i++){if(chunk[i]!=file[m->read_at+i])return SC32_E_INVALID;
                ((u8 *)m->metadata)[m->read_at+i]=chunk[i];
            }
            m->read_at+=n;
            remaining-=n;
            budget-=n;
        }
        if(remaining)return SC32_PENDING;
        m->phase=2;
        return SC32_PENDING;
    }
    u32 relocation_count=d2_word(file,SCL_H_RELOC_COUNT);
    while(m->verify_segment<m->info.segment_count && budget){
        const u8 *seg=file+d2_word(file,SCL_H_SEGMENTS)+m->verify_segment*SCL_SEGMENT_SIZE;
        u32 rva=d2_word(seg,SCL_S_RVA),memory=d2_word(seg,SCL_S_MEMORY_SIZE),payload=d2_word(seg,SCL_S_FILE_SIZE);
        if(!m->verify_at && !d2_mapping_access(TASK(pid).pd,m->info.base+rva,memory,d2_word(seg,SCL_S_FLAGS)&SCL_SEG_W))return SC32_E_ACCESS;
        while(m->verify_at<memory && budget){u8 actual[256];
            u32 n=memory-m->verify_at;
            if(n>sizeof(actual))n=sizeof(actual);
            if(n>budget)n=budget;
            if(paging_user_copy(TASK(pid).pd,m->info.base+rva+m->verify_at,actual,n,0))return SC32_E_RANGE;
            /* 真实调试拥有者可能在init前下INT3；比对原始代码视图，
             * 不能把合法断点当成坏模块，也不能跳过整条指令的验证。 */
            process_debug_original(pid,m->info.base+rva+m->verify_at,actual,n);
            for(u32 id=state->breaks;id;){d2_break_t *bp=d2_break(id);
                if(!bp)break;
                if(!(bp->flags&SC32_DEBUG_BP_INVALID) && bp->address>=m->info.base+rva+m->verify_at
                && bp->address-(m->info.base+rva+m->verify_at)<n){actual[bp->address-(m->info.base+rva+m->verify_at)]=bp->original;
                }
                id=bp->next;
            }
            for(u32 i=0;i<n;i++){u32 position=m->verify_at+i,at=rva+position;
                u8 expected=position<payload?file[d2_word(seg,SCL_S_FILE_OFFSET)+position]:0;
                while(m->reloc_index<relocation_count){const u8 *rel=file+d2_word(file,SCL_H_RELOCS)+m->reloc_index*SCL_RELOC_SIZE;
                    u32 patch=d2_word(rel,SCL_R_PATCH);
                    if(at>=patch+4){m->reloc_index++;
                        continue;
                    }
                    if(at>=patch){
                        if(m->proof_reloc_index!=m->reloc_index){error=d2_relocation(state,m,rel,&m->proof_reloc_value);
                            if(error)return error;
                            m->proof_reloc_index=m->reloc_index;
                        }
                        expected=(u8)(m->proof_reloc_value>>((at-patch)*8));
                    }break;
                }
                if(actual[i]!=expected)return SC32_E_INVALID;
            }
            m->verify_at+=n;
            budget-=n;
        }
        if(m->verify_at==memory){m->verify_segment++;
            m->verify_at=0;
        }
    }
    if(m->verify_segment<m->info.segment_count)return SC32_PENDING;
    fs_native_close(m->source_object,0);
    m->source_object=0;
    pframe_free_run(m->file,m->file_pages);
    m->file=m->file_pages=0;
    m->info.flags=SC32_DEBUG_VERIFIED_SOURCE|SC32_DEBUG_MODULE_INITIALIZING;
    m->next=state->modules;
    state->modules=m->info.id;
    state->module_count++;
    state->loading=0;
    state->module_epoch++;
    return 0;
}
static int d2_registration(int pid,u32 operation,u32 request,u32 output)
{
    Sc32DebugRegistration r;
    int error=sc32_core_input(pid,request,&r,sizeof(r));
    if(error)return error;
    d2_state_t *s=d2_state(pid,1);
    if(!s)return SC32_E_MEMORY;
    if(operation!=SC32_D2_REGISTER){if(output || !r.module || !r.module_generation)return SC32_E_INVALID;
        u32 *link=&s->modules;
        while(*link){d2_module_t *m=d2_module(*link);
            if(!m)return SC32_E_STALE;
            if(m->info.id==r.module && m->info.generation==r.module_generation && m->info.base==r.base){
                if(operation==SC32_D2_READY){if(!(m->info.flags&SC32_DEBUG_MODULE_READY) || (m->info.flags&SC32_DEBUG_MODULE_INITIALIZING)){
                        m->info.flags=(m->info.flags&~SC32_DEBUG_MODULE_INITIALIZING)|SC32_DEBUG_MODULE_READY;
                        s->module_epoch++;
                    }
                    return 0;
                }
                /* 注销先撤同映像断点，再移除登记；真实页仍由原MODULEFREE回收。 */
                u32 base=m->info.base,bytes=m->info.image_bytes;
                for(u32 id=s->breaks;id;){d2_break_t *b=d2_break(id);
                    if(!b)break;
                    if(!(b->flags&(SC32_DEBUG_BP_PENDING|SC32_DEBUG_BP_INVALID)) && b->address>=base && b->address-base<bytes){
                        (void)paging_debug_patch(TASK(pid).pd,b->address,b->original);
                    }
                    id=b->next;
                }
                sc32_debug_unmap(pid,base,bytes);
                return 0;
            }link=&m->next;
        }
        return SC32_E_STALE;
    }
    error=sc32_core_output(pid,output,sizeof(Sc32DebugRegistrationResult));
    if(error)return error;
    /* 固定ROOT由可信scl_boot在首条用户指令前登记。它已经执行的
     * loader全局数据不可再与零BSS比对；只借同一内核记录的精确
     * 映像/只读元数据身份，绝不为任意“borrowed”用户请求开例外。 */
    task_record_t *boot=task_record(pid);
    if(r.base==boot->scl_root_base && r.metadata==boot->scl_root_metadata && r.metadata_bytes==boot->scl_root_metadata_size){
        for(u32 id=s->modules;id;){d2_module_t *root=d2_module(id);
            if(!root)break;
            if(root->info.base==r.base && root->info.kind==SC32_DEBUG_SCL && d2_equal(root->info.name,"SCLDR.SCL")){
                Sc32DebugRegistrationResult result={sizeof(result),1,root->info.id,root->info.generation};
                return d2_publish(pid,output,&result,sizeof(result));
            }
            id=root->next;
        }
    }
    d2_module_t *m=s->loading?d2_module(s->loading):0;
    if(m){if(m->info.base!=r.base || m->input_metadata!=r.metadata || m->input_metadata_bytes!=r.metadata_bytes)return SC32_E_BUSY;
    }
    else{error=d2_module_begin(pid,s,&r,&m);
        if(error)return error;
    }
    error=d2_module_step(pid,s,m);
    if(error<0){s->loading=0;
        d2_module_drop(m);
        return error;
    }
    if(error==SC32_PENDING)return error;
    Sc32DebugRegistrationResult result={sizeof(result),1,m->info.id,m->info.generation};
    return d2_publish(pid,output,&result,sizeof(result));
}
int sc32_debug_main_image(int pid,const char *path,u32 load,u32 bss)
{
    if(!load || load>0xffffffffu-bss)return SC32_E_RANGE;
    d2_state_t *s=d2_state(pid,1);
    if(!s)return SC32_E_MEMORY;
    char canonical[64];
    u32 stat[2],meta[8];
    if(fs_normalize(path,canonical)<=0 || fs_stat(canonical,stat) || stat[0]!=1 || fs_metadata(canonical,meta))return SC32_E_IO;
    int index=objpool_alloc(&d2_modules);
    u32 generation=d2_next_serial();
    if(index<0 || !generation){if(index>=0)objpool_release(&d2_modules,index);
        return SC32_E_MEMORY;
    }
    d2_module_t *m=objpool_get(&d2_modules,index);
    d2_zero(m,sizeof(*m));
    m->owner=(u32)pid;
    m->owner_generation=task_generation(pid);
    m->info.size=sizeof(m->info);
    m->info.version=1;
    m->info.id=(u32)index+1;
    m->info.generation=generation;
    m->info.kind=SC32_DEBUG_MAIN;
    m->info.base=USER_BASE;
    m->info.image_bytes=load+bss;
    m->info.allocation_bytes=(load+bss+4095u)&~4095u;
    m->info.source_bytes=stat[1];
    m->info.source_revision=meta[7];
    m->info.flags=SC32_DEBUG_VERIFIED_SOURCE|SC32_DEBUG_MODULE_READY;
    m->info.segment_count=1;
    m->info.source[0]='/';
    d2_text(m->info.source+1,sizeof(m->info.source)-1,canonical);
    const char *name=canonical;
    for(const char *p=canonical;*p;p++)if(*p=='/')name=p+1;
    d2_text(m->info.name,sizeof(m->info.name),name);
    sha256_t hash;
    sha256_init(&hash);
    u8 chunk[512];
    for(u32 at=0;at<stat[1];){u32 n=stat[1]-at;
        if(n>sizeof(chunk))n=sizeof(chunk);
        if(fs_read_at(canonical,chunk,n,at)!=(int)n){d2_module_drop(m);
            return SC32_E_IO;
        }sha256_update(&hash,chunk,n);
        at+=n;
    }
    sha256_final(&hash,m->info.sha256);
    m->next=s->modules;
    s->modules=m->info.id;
    s->module_count++;
    s->module_epoch++;
    return 0;
}
int sc32_debug_bootstrap_image(int pid,u32 base,const void *source,u32 file_bytes,u32 allocation_bytes)
{
    /* 调用者只有刚完成root_copy的可信scl_boot；源是同一已完整
     * 验证文件。此时ROOT尚未运行，真实来源、映像和BSS由加载器
     * 直接建立，不必经三环重读文件或信任自报身份。 */
    d2_state_t *s=d2_state(pid,1);
    if(!s)return SC32_E_MEMORY;
    const u8 *file=source;
    u32 metadata_bytes=d2_word(file,SCL_H_STRINGS)+d2_word(file,SCL_H_STRINGS_SIZE);
    int index=objpool_alloc(&d2_modules);
    u32 generation=d2_next_serial();
    if(index<0 || !generation){if(index>=0)objpool_release(&d2_modules,index);
        return SC32_E_MEMORY;
    }
    d2_module_t *m=objpool_get(&d2_modules,index);
    d2_zero(m,sizeof(*m));
    m->owner=(u32)pid;
    m->owner_generation=task_generation(pid);
    m->info.size=sizeof(m->info);
    m->info.version=1;
    m->info.id=(u32)index+1;
    m->info.generation=generation;
    m->metadata_pages=(metadata_bytes+4095u)/4096u;
    m->metadata=pframe_alloc_run(m->metadata_pages);
    if(!m->metadata){d2_module_drop(m);
        return SC32_E_MEMORY;
    }
    for(u32 i=0;i<metadata_bytes;i++)((u8 *)m->metadata)[i]=file[i];
    m->info.kind=SC32_DEBUG_SCL;
    m->info.base=base;
    m->info.image_bytes=d2_word(file,SCL_H_IMAGE_SIZE);
    m->info.allocation_bytes=allocation_bytes;
    m->info.abi_major=d2_word(file,SCL_H_ABI_MAJOR);
    m->info.abi_minor=d2_word(file,SCL_H_ABI_MINOR);
    m->info.library_version=d2_word(file,SCL_H_LIBRARY_VERSION);
    m->info.segment_count=d2_word(file,SCL_H_SEGMENT_COUNT);
    m->info.symbol_count=d2_word(file,SCL_H_EXPORT_COUNT);
    m->info.source_bytes=file_bytes;
    u32 metadata[8];
    if(!fs_metadata("/SYS/LIB/SCLDR.SCL",metadata))m->info.source_revision=metadata[7];
    d2_text(m->info.name,sizeof(m->info.name),"SCLDR.SCL");
    d2_text(m->info.source,sizeof(m->info.source),"/SYS/LIB/SCLDR.SCL");
    sha256(file,file_bytes,m->info.sha256);
    m->info.flags=SC32_DEBUG_VERIFIED_SOURCE|SC32_DEBUG_MODULE_READY;
    m->next=s->modules;
    s->modules=m->info.id;
    s->module_count++;
    s->module_epoch++;
    return 0;
}
