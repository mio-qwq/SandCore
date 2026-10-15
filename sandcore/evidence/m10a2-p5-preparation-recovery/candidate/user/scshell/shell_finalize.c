void ScShellFinalize(void) {
    ShObject *o;
    ScU32 i;
    static const ScU32 order[]= {
        SH_MENU,SH_DIALOG,SH_SERVICE,SH_CLIENT,SH_JOB,SH_REQUEST,SH_STORE,SH_CATALOG,SH_SHORTCUT,SH_APP,SH_LAYOUT
    };
    if(sh_quiet)return;
    sh_quiet=1;
    sh_finalizing=1;
    while(sh_providers) {
        ShProvider *p=sh_providers;
        sh_providers=p->next;
        p->removed=1;
        sh_menu_provider_release(p);
    }
    /* 先撤所有跨进程端点/worker与UI listener，后释放纯值快照。此时
     * 应用已经结束，绝不能再执行应用context_drop或完成回调。依赖SCUI
     * 的对象留给它紧随其后的quiet fini，避免普通Release复活用户代码。 */
    for(o=sh_objects;o;o=o->next)o->closing=1;
    /* 第一遍只析构资源而不free任一Shell对象，跨对象Release不能使
     * 后面的owner字段悬空；第二遍统一还metadata，复杂度O(10*n)。 */
    for(i=0;i<sizeof(order)/sizeof(order[0]);++i)for(o=sh_objects;o;o=o->next)if(o->kind==order[i])o->dispose(o);
    while(sh_objects) {
        o=sh_objects;
        sh_objects=o->next;
        o->magic=0;
        free(o);
    }
    if(sh_icons) {
        ScCacheRelease(sh_icons);
        sh_icons=0;
    }
    sh_icon_ui=0;
}
