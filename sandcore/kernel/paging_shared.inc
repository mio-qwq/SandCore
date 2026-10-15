/* 真实Shared PTE正文由同批Broker管理PF引用；源码已接线，尚未统一构建/运行。
 * 只管理本人60000000窗口；旧heap/SCL/SCX的私有释放合同保留。 */
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
    /* 分步Unmap仍拥有整块VA，不能把已拆前缀变成新的reserve空洞。
     * 否则同PID交错另一个Map会覆盖A前缀，A最终Release误碰B的PTE。
     * 只撤真实映射，保留块首/尾与Shared占位至完整Release原子清除。 */
    for(u32 i=0;i<count;i++){
        u32 *pte=private_pte(pd,base+(index+i)*PAGE_SIZE,0);
        *pte&=PTE_SHARED|PTE_BLOCK_FIRST|PTE_BLOCK_LAST;
    }
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
        if(!pte || !(*pte&PTE_SHARED) || (*pte&PTE_P))return -1;
        /* 完整块边界同样属于内核reservation；不准用子范围消掉
         * 邻接候选的首/尾，预验全部完成后才改变任何一个PTE。 */
        if((address==base)!=((*pte&PTE_BLOCK_FIRST)!=0)
            || (address==base+bytes-PAGE_SIZE)!=((*pte&PTE_BLOCK_LAST)!=0))return -1;
    }
    for(u32 address=base;address<base+bytes;address+=PAGE_SIZE)*private_pte(pd,address,0)=0;
    private_trim(pd,USER_SHARED_BASE,USER_SHARED_TOP);
    return 0;
}
