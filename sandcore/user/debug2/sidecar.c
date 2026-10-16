#include "D2APP.H"
#include "SCDBGFILE.H"

static ScU32 d2_sdbg_word(const unsigned char *p,ScU32 offset)
{return (ScU32)p[offset]|((ScU32)p[offset+1]<<8)|((ScU32)p[offset+2]<<16)|((ScU32)p[offset+3]<<24);
}
static int d2_sdbg_span(ScU32 *cursor,ScU32 count,ScU32 stride,ScU32 offset,ScU32 total)
{
    if(offset!=*cursor || count>(UINT_MAX-*cursor)/stride)return 0;
     *cursor+=count*stride;
    return *cursor<=total;
}
static const char *d2_sdbg_string(D2Frame *f,ScU32 offset,ScU32 maximum)
{
    unsigned char *p=f->sidecar_bytes;
    ScU32 size=d2_sdbg_word(p,SCDBG_H_STRINGS_SIZE),base=d2_sdbg_word(p,SCDBG_H_STRINGS);
    if(offset>=size)return 0;
    const char *s=(const char *)p+base+offset;
    ScU32 n=0;
    while(offset+n<size && s[n]){if((unsigned char)s[n]<32 || n==maximum)return 0;
        n++;
    }
    if(offset+n==size || !n || ScUtf8Validate(s,n,0)<0)return 0;
    return s;
}
static int d2_sdbg_address(const ScDebugModule *m,ScU32 value,ScU32 *out)
{
    if(m->kind==SC32_DEBUG_SCL){if(value>UINT_MAX-m->base)return 0;
        value+=m->base;
    }*out=value;
    return 1;
}
static ScStatus d2_sdbg_header(D2Frame *f,const ScDebugModule *m)
{
    const unsigned char *p=f->sidecar_bytes;
    if(f->sidecar_size<SCDBG_HEADER_SIZE || memcmp(p,SCDBG_MAGIC,8)
    || d2_sdbg_word(p,SCDBG_H_VERSION)!=SCDBG_VERSION
    || d2_sdbg_word(p,SCDBG_H_HEADER_SIZE)!=SCDBG_HEADER_SIZE
    || d2_sdbg_word(p,SCDBG_H_MACHINE)!=SCDBG_MACHINE_I386
    || d2_sdbg_word(p,SCDBG_H_ADDRESS_KIND)!=(m->kind==SC32_DEBUG_SCL?SCDBG_ADDRESS_RVA:SCDBG_ADDRESS_VA)
    || d2_sdbg_word(p,SCDBG_H_IMAGE_FILE_SIZE)!=m->source_bytes
    || !(m->flags&SC32_DEBUG_VERIFIED_SOURCE)
    || memcmp(p+SCDBG_H_IMAGE_SHA256,m->sha256,32) || !d2_sdbg_word(p,SCDBG_H_OBJECT_COUNT))return SC32_E_STALE;
    ScU32 cursor=SCDBG_HEADER_SIZE;
    if(!d2_sdbg_span(&cursor,d2_sdbg_word(p,SCDBG_H_SYMBOL_COUNT),SCDBG_SYMBOL_SIZE,d2_sdbg_word(p,SCDBG_H_SYMBOLS),f->sidecar_size)
    || !d2_sdbg_span(&cursor,d2_sdbg_word(p,SCDBG_H_FILE_COUNT),SCDBG_FILE_SIZE,d2_sdbg_word(p,SCDBG_H_FILES),f->sidecar_size)
    || !d2_sdbg_span(&cursor,d2_sdbg_word(p,SCDBG_H_LINE_COUNT),SCDBG_LINE_SIZE,d2_sdbg_word(p,SCDBG_H_LINES),f->sidecar_size)
    || !d2_sdbg_word(p,SCDBG_H_STRINGS_SIZE)
    || !d2_sdbg_span(&cursor,d2_sdbg_word(p,SCDBG_H_STRINGS_SIZE),1,d2_sdbg_word(p,SCDBG_H_STRINGS),f->sidecar_size)
    || cursor!=f->sidecar_size || p[cursor-1])return SC32_E_INVALID;
    return 0;
}
static ScStatus d2_sdbg_validate_record(D2Frame *f,const ScDebugModule *m,ScU32 kind,ScU32 index)
{
    const unsigned char *p=f->sidecar_bytes;
    ScU32 objects=d2_sdbg_word(p,SCDBG_H_OBJECT_COUNT);
    if(kind==0){const unsigned char *r=p+d2_sdbg_word(p,SCDBG_H_SYMBOLS)+index*SCDBG_SYMBOL_SIZE;
        ScU32 address,bytes=d2_sdbg_word(r,SCDBG_S_SIZE),symbol_kind=d2_sdbg_word(r,SCDBG_S_KIND);
        if(!d2_sdbg_string(f,d2_sdbg_word(r,SCDBG_S_NAME),127)
        || !d2_sdbg_address(m,d2_sdbg_word(r,SCDBG_S_ADDRESS),&address)
        || (symbol_kind!=1 && symbol_kind!=2) || d2_sdbg_word(r,SCDBG_S_BINDING)>1
        || d2_sdbg_word(r,SCDBG_S_OBJECT)>=objects
        || !D2AddressInModule(f,m,address,bytes,symbol_kind==1?SC32_DEBUG_EXECUTABLE:SC32_DEBUG_READABLE))return SC32_E_INVALID;
    }else if(kind==1){const unsigned char *r=p+d2_sdbg_word(p,SCDBG_H_FILES)+index*SCDBG_FILE_SIZE;
        if(d2_sdbg_word(r,SCDBG_F_OBJECT)>=objects || !d2_sdbg_string(f,d2_sdbg_word(r,SCDBG_F_PATH),4095)
        || d2_sdbg_word(r,16))return SC32_E_INVALID;
    }else{const unsigned char *r=p+d2_sdbg_word(p,SCDBG_H_LINES)+index*SCDBG_LINE_SIZE;
        ScU32 address,file_index=d2_sdbg_word(r,SCDBG_L_FILE),object=d2_sdbg_word(r,SCDBG_L_OBJECT);
        if(!d2_sdbg_address(m,d2_sdbg_word(r,SCDBG_L_ADDRESS),&address)
        || !D2AddressInModule(f,m,address,1,SC32_DEBUG_EXECUTABLE) || !d2_sdbg_word(r,SCDBG_L_LINE)
        || file_index>=d2_sdbg_word(p,SCDBG_H_FILE_COUNT) || object>=objects)return SC32_E_INVALID;
        const unsigned char *source=p+d2_sdbg_word(p,SCDBG_H_FILES)+file_index*SCDBG_FILE_SIZE;
        if(d2_sdbg_word(source,SCDBG_F_OBJECT)!=object)return SC32_E_INVALID;
    }
    return 0;
}
static void d2_sdbg_next(D2Frame *f)
{
    if(f->sidecar_file)ScFileClose(f->sidecar_file);
    f->sidecar_file=0;
    free(f->sidecar_bytes);
    f->sidecar_bytes=0;
    f->sidecar_stage=f->sidecar_cursor=f->sidecar_read=f->sidecar_size=0;
    f->sidecar_module++;
}
ScStatus D2SidecarStep(D2App *a,D2Frame *f)
{
    (void)a;
    if(f->sidecar_module>=D2Count(f->modules))return SC_UI_END;
    ScDebugModule module;
    if(ScArrayGet(f->modules,f->sidecar_module,&module)<0)return SC32_E_STALE;
    if(!f->sidecar_stage){
        char path[65];
        ScU32 n=(ScU32)strlen(module.source);
        if(n+5>=sizeof(path)){snprintf(f->sidecar_status,sizeof(f->sidecar_status),"Sidecar path exceeds SandFS name limit");
            d2_sdbg_next(f);
            return 0;
        }
        snprintf(path,sizeof(path),"%s.sdbg",module.source);
        ScFileOpenDesc desc;
        D2Record(&desc,sizeof(desc));
        desc.path=path;
        desc.flags=SC32_FILE_READ;
        ScStatus s=ScFileOpen(&desc,&f->sidecar_file);
        if(s==SC32_E_NOT_FOUND){d2_sdbg_next(f);
            return 0;
        }
        if(s<0){snprintf(f->sidecar_status,sizeof(f->sidecar_status),"Sidecar unavailable: %d",s);
            d2_sdbg_next(f);
            return 0;
        }
        ScFileInfo info;
        D2Record(&info,sizeof(info));
        s=ScFileQuery(f->sidecar_file,&info);
        if(s<0 || info.length<SCDBG_HEADER_SIZE || info.length>16u*1024u*1024u){snprintf(f->sidecar_status,sizeof(f->sidecar_status),"Sidecar rejected: invalid length");
            d2_sdbg_next(f);
            return 0;
        }
        f->sidecar_size=info.length;
        f->sidecar_bytes=malloc(info.length);
        if(!f->sidecar_bytes)return SC32_E_MEMORY;
        f->sidecar_stage=1;
        return 0;
    }
    if(f->sidecar_stage==1){ScU32 bytes=f->sidecar_size-f->sidecar_read;
        if(bytes>65536)bytes=65536;
        ScIoResult io;
        D2Record(&io,sizeof(io));
        ScStatus s=ScFileRead(f->sidecar_file,f->sidecar_bytes+f->sidecar_read,bytes,&io);
        if(s<0 || io.transferred!=bytes){snprintf(f->sidecar_status,sizeof(f->sidecar_status),"Sidecar rejected: short read");
            d2_sdbg_next(f);
            return 0;
        }
        f->sidecar_read+=bytes;
        if(f->sidecar_read==f->sidecar_size){s=d2_sdbg_header(f,&module);
            if(s<0){snprintf(f->sidecar_status,sizeof(f->sidecar_status),"Sidecar rejected: image identity or format (%d)",s);
                d2_sdbg_next(f);
                return 0;
            }
            f->sidecar_stage=2;
            f->sidecar_cursor=0;
        }return 0;
    }
    const unsigned char *p=f->sidecar_bytes;
    if(f->sidecar_stage>=2 && f->sidecar_stage<=4){ScU32 kind=f->sidecar_stage-2;
        ScU32 count=d2_sdbg_word(p,kind==0?SCDBG_H_SYMBOL_COUNT:kind==1?SCDBG_H_FILE_COUNT:SCDBG_H_LINE_COUNT);
        for(ScU32 work=0;work<128 && f->sidecar_cursor<count;work++,f->sidecar_cursor++){
            ScStatus s=d2_sdbg_validate_record(f,&module,kind,f->sidecar_cursor);
            if(s<0){snprintf(f->sidecar_status,sizeof(f->sidecar_status),"Sidecar rejected: record outside verified module");
                d2_sdbg_next(f);
                return 0;
        }}
        if(f->sidecar_cursor==count){f->sidecar_stage++;
            f->sidecar_cursor=0;
        }return 0;
    }
    if(f->sidecar_stage==5){ScU32 count=d2_sdbg_word(p,SCDBG_H_SYMBOL_COUNT);
        for(ScU32 work=0;work<128 && f->sidecar_cursor<count;work++,f->sidecar_cursor++){
            const unsigned char *r=p+d2_sdbg_word(p,SCDBG_H_SYMBOLS)+f->sidecar_cursor*SCDBG_SYMBOL_SIZE;
            D2Row row;
            memset(&row,0,sizeof(row));
            d2_sdbg_address(&module,d2_sdbg_word(r,SCDBG_S_ADDRESS),&row.address);
            row.aux=module.id;
            row.generation=module.generation;
            row.flags=d2_sdbg_word(r,SCDBG_S_SIZE);
            const char *name=d2_sdbg_string(f,d2_sdbg_word(r,SCDBG_S_NAME),127);
            snprintf(row.text[0],sizeof(row.text[0]),"%s",module.name);
            snprintf(row.text[1],sizeof(row.text[1]),"%s",name);
            snprintf(row.text[2],sizeof(row.text[2]),"%08X",row.address);
            snprintf(row.text[3],sizeof(row.text[3]),d2_sdbg_word(r,SCDBG_S_KIND)==1?"function / %uB":"data / %uB",row.flags);
            snprintf(row.text[4],sizeof(row.text[4]),"verified .sdbg / %s TU %u",d2_sdbg_word(r,SCDBG_S_BINDING)?"global":"local",d2_sdbg_word(r,SCDBG_S_OBJECT));
            /* 重复导出只替换同一真实地址/名字的说明，TU本地符号保留
             * 独立行；同名局部变量不能合并成一个全局身份。 */
            int replaced=0;
            if(d2_sdbg_word(r,SCDBG_S_BINDING)){const D2Row *existing=D2Data(f->rows[D2_SYMBOLS]);
                for(ScU32 i=0;i<D2Count(f->rows[D2_SYMBOLS]);i++)if(existing[i].aux==module.id && existing[i].address==row.address && !strcmp(existing[i].text[1],name)){
                    row.id=existing[i].id;
                    ScStatus s=ScArraySet(f->rows[D2_SYMBOLS],i,&row);
                    if(s<0)return s;
                    replaced=1;
                    break;
            }}
            if(!replaced){ScStatus s=D2RowAppend(f->rows[D2_SYMBOLS],&row);
                if(s<0)return s;
            }
        }
        if(f->sidecar_cursor==count){f->sidecar_stage=6;
            f->sidecar_cursor=0;
        }return 0;
    }
    ScU32 count=d2_sdbg_word(p,SCDBG_H_LINE_COUNT);
    for(ScU32 work=0;work<128 && f->sidecar_cursor<count;work++,f->sidecar_cursor++){
        const unsigned char *r=p+d2_sdbg_word(p,SCDBG_H_LINES)+f->sidecar_cursor*SCDBG_LINE_SIZE;
        ScU32 file_index=d2_sdbg_word(r,SCDBG_L_FILE);
        const unsigned char *file=p+d2_sdbg_word(p,SCDBG_H_FILES)+file_index*SCDBG_FILE_SIZE;
        D2SourceLine line;
        memset(&line,0,sizeof(line));
        d2_sdbg_address(&module,d2_sdbg_word(r,SCDBG_L_ADDRESS),&line.address);
        line.module=module.id;
        line.line=d2_sdbg_word(r,SCDBG_L_LINE);
        line.object=d2_sdbg_word(r,SCDBG_L_OBJECT);
        snprintf(line.path,sizeof(line.path),"%s",d2_sdbg_string(f,d2_sdbg_word(file,SCDBG_F_PATH),4095));
        ScStatus s=D2Append(f->lines,&line);
        if(s<0)return s;
    }
    if(f->sidecar_cursor==count){snprintf(f->sidecar_status,sizeof(f->sidecar_status),"Verified .sdbg symbols and source lines");
        d2_sdbg_next(f);
    }return 0;
}
