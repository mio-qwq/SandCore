/* 此文件是paging.c的真实增量草稿，尚未包含/构建。Root在Std批结束后
* 才可将它放到private_pte/private_flush/private_trim定义之后。
* 必须同批完成：new_task_dir清空SHARED的16个PDE；user_range接受窗口；
* free_task_dir不free带PTE_SHARED的PF；debug_patch拒绝共享页；
* map_device拒绝此VA窗；broker_stop_step完成后才释放任务页目录。
* 这些是显式接线前置，当前旧paging源保持不动、shared cap仍为0。 */
#define PTE_SHARED 0x800u
#define USER_SHARED_BASE 0x60000000u
#define USER_SHARED_TOP 0x64000000u
#define SHARED_STEP_PAGES 16u

u32 paging_shared_reserve(u32 pd,u32 bytes)
{
    if(!pd || pd==paging_kernel_pd() || !bytes || (bytes&4095u) || bytes>USER_SHARED_TOP-USER_SHARED_BASE)return 0;
    u32 pages=bytes/PAGE_SIZE,run=0,base=0;
    for(u32 address=USER_SHARED_BASE;address<USER_SHARED_TOP;address+=PAGE_SIZE){
        u32 *pte=private_pte(pd,address,0);
        if(pte && (*pte&(PTE_P|PTE_SHARED)))run=0;
        else if(++run==pages){base=address-(pages-1u)*PAGE_SIZE;
            break;
        }
    }
    if(!base)return 0;
    u32 done=0;
    for(;done<pages;done++){
        u32 *pte=private_pte(pd,base+done*PAGE_SIZE,1);
        if(!pte)break;
        *pte=PTE_SHARED;
    }
    if(done!=pages){
        for(u32 i=0;i<done;i++)*private_pte(pd,base+i*PAGE_SIZE,0)=0;
        private_trim(pd,USER_SHARED_BASE,USER_SHARED_TOP);
        return 0;
    }
    /* 这里只留PTE占位，不申请/清零64MiB正文。最多16张PT、16384个
    * 元数据项的工作与实际shared PF初始化分开，后者由broker短步做。 */
    *private_pte(pd,base,0)|=PTE_BLOCK_FIRST;
    *private_pte(pd,base+bytes-PAGE_SIZE,0)|=PTE_BLOCK_LAST;
    private_flush(pd);
    return base;
}

static int shared_batch(u32 pd,u32 base,u32 index,u32 count)
{
    return pd && pd!=paging_kernel_pd() && base>=USER_SHARED_BASE && base<USER_SHARED_TOP
    && !(base&4095u) && count && count<=SHARED_STEP_PAGES
    && index<(USER_SHARED_TOP-base)/PAGE_SIZE && count<=(USER_SHARED_TOP-base)/PAGE_SIZE-index;
}

int paging_shared_bind(u32 pd,u32 base,u32 index,const u32 *frames,u32 count,u32 access)
{
    if(!shared_batch(pd,base,index,count) || !frames || (access!=1 && access!=3))return -1;
    for(u32 i=0;i<count;i++){
        u32 *pte=private_pte(pd,base+(index+i)*PAGE_SIZE,0);
        if(!pte || !(*pte&PTE_SHARED) || (*pte&PTE_P) || !frames[i] || (frames[i]&4095u))return -1;
    }
    for(u32 i=0;i<count;i++){
        u32 *pte=private_pte(pd,base+(index+i)*PAGE_SIZE,0);
        *pte=frames[i]|PTE_P|PTE_US|(access==3?PTE_RW:0)|(*pte&(PTE_SHARED|PTE_BLOCK_FIRST|PTE_BLOCK_LAST));
    }
    private_flush(pd);
    return 0;
}

int paging_shared_unbind(u32 pd,u32 base,u32 index,u32 count)
{
    if(!shared_batch(pd,base,index,count))return -1;
    for(u32 i=0;i<count;i++){
        u32 *pte=private_pte(pd,base+(index+i)*PAGE_SIZE,0);
        if(!pte || !(*pte&PTE_SHARED))return -1;
    }
    for(u32 i=0;i<count;i++)*private_pte(pd,base+(index+i)*PAGE_SIZE,0)=0;
    /* 不pframe_free：另一个进程可能仍有RO/RW映射。PF引用只归broker
    * 对象，不把PTE数量直接等同物理拥有权，更不复制正文伪造共享。 */
    private_flush(pd);
    return 0;
}

int paging_shared_release(u32 pd,u32 base,u32 bytes)
{
    if(!pd || pd==paging_kernel_pd() || base<USER_SHARED_BASE || base>=USER_SHARED_TOP
    || !bytes || ((base|bytes)&4095u) || bytes>USER_SHARED_TOP-base)return -1;
    for(u32 address=base;address<base+bytes;address+=PAGE_SIZE){
        u32 *pte=private_pte(pd,address,0);
        if(pte && *pte)return -1;
    }
    private_trim(pd,USER_SHARED_BASE,USER_SHARED_TOP);
    return 0;
}
