/* 只由kind9的小尺寸暂停路径调用；不可见正文不接受普通编辑键。
 * ^X后仅Y/N/Esc有意义，未保存新文件须恢复尺寸再选择路径。 */
static int nano_small_key(nano_state *n,int key)
{
    if(key==NK_RESIZE){
        n->size_exit=0;
        return 0;
    }
    if(key==24){
        if(n->revision==n->clean_revision)return 1;
        n->size_exit=1;
        return 0;
    }
    if(!n->size_exit)return 0;
    if(key==27){
        n->size_exit=0;
        return 0;
    }
    if(key=='n' || key=='N')return 1;
    if(key=='y' || key=='Y'){
        if(!n->name[0])copy(n->notice,"Resize to choose a file; N discards",128);
        else if(nano_save(n,n->name)>=0)return 1;
        else copy(n->notice,"Save failed; buffer kept. Resize or N discards",128);
    }
    return 0;
}
