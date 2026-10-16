/* 包含于ui_controls.c末尾，真正属于共享SCUI，应用不接触UiControl。
 * 生命周期移除不允许veto；先提交record/可见性，再发只读选择通知。 */
#include "../SCUITABS.H"
static int ui_tab_forget(UiNode *tabs,ScUiNode page)
{
    if(!tabs||tabs->kind!=SC_UI_TAB||(tabs->private_flags&UI_NODE_RETIRED)||!tabs->control)return 0;
    UiControl *c=ui_control(tabs);
    UiTabItem **slot=&c->tabs,*previous=0;
    while(*slot&&(*slot)->page!=page){
        UiNode *n=ui_node(tabs->owner,(*slot)->page);
        if(n&&n->parent==tabs)previous=*slot;
        slot=&(*slot)->next;
    }
    UiTabItem *item=*slot;
    if(!item)return 0;
    ScUiNode replacement=0;
    for(UiTabItem *walk=item->next;walk;walk=walk->next){
        UiNode *n=ui_node(tabs->owner,walk->page);
        if(n&&n->parent==tabs){
            replacement=n->id;
            break;
        }
    }
    if(!replacement&&previous)replacement=previous->page;
    *slot=item->next;
    ScStringRelease(item->title);
    free(item);
    int selected=c->selected_page==page;
    if(selected){
        c->selected_page=replacement;
        tabs->value.value.lo=replacement;
        for(UiTabItem *walk=c->tabs;walk;walk=walk->next){
            UiNode *n=ui_node(tabs->owner,walk->page);
            if(!n||n->parent!=tabs)continue;
            n->state=(n->state&~SC_UI_VISIBLE)|(n->id==replacement?SC_UI_VISIBLE:0);
            ui_tree_effective(n);
        }
    }
    ui_node_layout_dirty(tabs);
    ui_node_damage(tabs);
    if(selected){
        ScStatus notice=ui_event(tabs,SC_UI_EVENT_SELECTION_CHANGED,&replacement,sizeof(replacement));
        if(notice<0)tabs->owner->notification_error=notice;
    }
    return 1;
}
ScStatus ScUiTabSetText(ScUiContext *context,ScUiNode id,ScUiNode page,const ScUiText *title)
{
    UiNode *tabs=ui_node(context,id);
    if(!tabs)return SC_UI_E_STALE;
    if(tabs->kind!=SC_UI_TAB)return SC_UI_E_INVALID;
    UiControl *c=ui_control(tabs);
    UiTabItem *item=c->tabs;
    while(item&&item->page!=page)item=item->next;
    if(!item)return SC_UI_E_NOT_FOUND;
    ScString *candidate=0;
    ScStatus s=ui_text_copy(title,&candidate);
    if(s<0)return s;
    ScStringRelease(item->title);
    item->title=candidate;
    ui_node_layout_dirty(tabs);
    ui_node_damage(tabs);
    return 0;
}
ScStatus ScUiTabRemove(ScUiContext *context,ScUiNode id,ScUiNode page)
{
    UiNode *tabs=ui_node(context,id),*node=ui_node(context,page);
    if(!tabs||!node)return SC_UI_E_STALE;
    if(tabs->kind!=SC_UI_TAB || node->parent!=tabs)return SC_UI_E_INVALID;
    UiControl *c=ui_control(tabs);
    UiTabItem *item=c->tabs;
    while(item&&item->page!=page)item=item->next;
    if(!item)return SC_UI_E_NOT_FOUND;
    return ScUiNodeDestroy(context,page);
}
