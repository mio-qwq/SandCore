/* 像素仅从命名角色取色。Classic与Aurora共用命中、选择和布局模型，
 * 真图标保留内容色；资源缺失的回退仍区分目录与文档轮廓。 */
static void desk_frame(ScUiPainter *p,const ScUiRect *r,ScU32 role)
{
    ScUiPainterLine(p,r->x,r->y,r->x+r->width-1,r->y,role);
    ScUiPainterLine(p,r->x,r->y,r->x,r->y+r->height-1,role);
    ScUiPainterLine(p,r->x,r->y+r->height-1,r->x+r->width-1,r->y+r->height-1,role);
    ScUiPainterLine(p,r->x+r->width-1,r->y,r->x+r->width-1,r->y+r->height-1,role);
}
static ScStatus desk_paint_item(ScUiPainter *p,ScU32 i)
{
    DeskItem *item=desk.items+i;ScUiRect r=desk_item_rect(i);
    if(item->removed)return 0;
    if(item->selected){ScUiPainterFill(p,&r,SC32_COLOR_SELECT);desk_frame(p,&r,SC32_COLOR_ACCENT);}
    if(i==desk.drop_target)desk_frame(p,&r,SC32_COLOR_GOLD);
    ScI32 pixels=desk_scale(48);ScUiRect icon={r.x+(r.width-pixels)/2,r.y+desk_scale(8),pixels,pixels};
    if(item->icon)ScUiPainterIcon(p,&icon,item->icon,item->selected?SC_UI_SELECTED:0);
    else if(item->kind==SC32_PATH_DIRECTORY){
        ScUiRect tab={icon.x+2,icon.y+pixels/5,pixels/2,pixels/5};
        ScUiPainterFill(p,&tab,SC32_COLOR_GOLD);
        ScUiRect body={icon.x+2,icon.y+pixels/3,pixels-4,pixels*3/5};
        ScUiPainterFill(p,&body,SC32_COLOR_GOLD);desk_frame(p,&body,SC32_COLOR_SHADOW);
        ScUiPainterLine(p,body.x+1,body.y+1,body.x+body.width-2,body.y+1,SC32_COLOR_HIGHLIGHT);
    }else{
        ScUiRect page={icon.x+pixels/4,icon.y+2,pixels/2,pixels-4};
        ScUiPainterFill(p,&page,SC32_COLOR_PAPER);desk_frame(p,&page,SC32_COLOR_LINE);
        for(ScI32 j=0;j<3;j++)ScUiPainterLine(p,page.x+4,page.y+pixels/3+j*5,
            page.x+page.width-5,page.y+pixels/3+j*5,SC32_COLOR_MUTED);
    }
    if(item->shortcut){ScUiRect marker={icon.x,icon.y+pixels-13,13,13};
        ScUiPainterFill(p,&marker,SC32_COLOR_PAPER);desk_frame(p,&marker,SC32_COLOR_LINE);
        ScUiPainterLine(p,marker.x+3,marker.y+9,marker.x+9,marker.y+3,SC32_COLOR_ACCENT);
        ScUiPainterLine(p,marker.x+5,marker.y+3,marker.x+9,marker.y+3,SC32_COLOR_ACCENT);
        ScUiPainterLine(p,marker.x+9,marker.y+3,marker.x+9,marker.y+7,SC32_COLOR_ACCENT);
    }
    ScUiRect label={r.x+desk_scale(4),icon.y+pixels+desk_scale(5),r.width-desk_scale(8),r.height-pixels-desk_scale(18)};
    ScUiText text=desk_text(item->label);
    ScStatus s=ScUiPainterTextColor(p,&label,&text,SC_UI_FONT_BODY,SC_UI_TEXT_WRAP|SC_UI_TEXT_CENTER,
        item->selected?SC32_COLOR_SELECT_TEXT:SC32_COLOR_DESKTOP_TEXT);
    if(i==desk.focus){ScUiRect outline=r;outline.x+=2;outline.y+=2;outline.width-=4;outline.height-=4;
        desk_frame(p,&outline,item->selected?SC32_COLOR_SELECT_TEXT:SC32_COLOR_DESKTOP_TEXT);}
    return s;
}
static ScStatus desk_paint(ScUiContext *ui,ScUiNode node,ScUiPainter *p,void *context)
{
    (void)context;ScUiNodeInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus s=ScUiNodeQuery(ui,node,&info);if(s<0)return s;
    ScUiRect area={0,0,info.rectangle.width,info.rectangle.height};
    s=ScUiPainterClear(p,&area);if(s<0)return s;
    ScUiRect damage;s=ScUiPainterBounds(p,&damage);if(s<0)return s;
    /* 绘制由库真实damage裁剪，模型无改变时不重新生成帧。查询可见cell，
     * 模型总量不转化成每个鼠标包扫描全部图标的成本。 */
    ScI32 logical_bottom=(damage.y+damage.height+desk.scroll_y)*100/(ScI32)desk.metrics.scale_percent;
    ScI32 first=desk_floor((damage.y+desk.scroll_y)*100/(ScI32)desk.metrics.scale_percent,(ScI32)desk.grid_h)-1;
    ScI32 last=desk_floor(logical_bottom,(ScI32)desk.grid_h);
    ScI32 left=desk_floor((damage.x+desk.scroll_x)*100/(ScI32)desk.metrics.scale_percent,(ScI32)desk.grid_w)-1;
    ScI32 columns=desk_floor((damage.x+damage.width+desk.scroll_x)*100/(ScI32)desk.metrics.scale_percent,(ScI32)desk.grid_w);
    for(ScI32 y=first;y<=last;y++)for(ScI32 x=left;x<=columns;x++){
        DeskCell *cell=desk.cell_capacity?desk_cell(x,y):0;if(!cell || !cell->used)continue;
        for(ScU32 i=cell->first;i!=DESK_NONE;i=desk.items[i].next_cell){
            if(desk.gesture==DESK_DRAG && desk.moved && desk.items[i].selected)continue;
            ScUiRect r=desk_item_rect(i);if(!desk_intersects(&r,&damage))continue;
            s=desk_paint_item(p,i);if(s<0)return s;
        }
    }
    if(desk.gesture==DESK_DRAG && desk.moved)for(ScU32 i=0;i<desk.drag_count;i++){
        ScUiRect r=desk_item_rect(desk.drag_items[i]);if(!desk_intersects(&r,&damage))continue;
        s=desk_paint_item(p,desk.drag_items[i]);if(s<0)return s;}
    if(desk.gesture==DESK_BOX && desk.moved)desk_frame(p,&desk.rubber,SC32_COLOR_DESKTOP_TEXT);
    return 0;
}
static void desk_select(ScU32 i,ScU32 value)
{
    if(i<desk.count && !desk.items[i].removed && desk.items[i].selected!=value){desk.items[i].selected=value;desk_invalidate_item(i);}
}
static void desk_select_only(ScU32 index)
{
    for(ScU32 i=0;i<desk.count;i++)desk_select(i,i==index);
}
static void desk_focus_item(ScU32 index)
{
    if(index>=desk.count || desk.items[index].removed)return;
    ScU32 previous=desk.focus;desk.focus=index;
    desk_invalidate_item(previous);desk_invalidate_item(index);
    ScUiRect r=desk_item_rect(index);ScI32 old=desk.scroll_y,old_x=desk.scroll_x;
    if(r.x<0)desk.scroll_x+=r.x;
    else if(r.x+r.width>(ScI32)desk.host.work_width)desk.scroll_x+=r.x+r.width-(ScI32)desk.host.work_width;
    if(r.y<0)desk.scroll_y+=r.y;
    else if(r.y+r.height>(ScI32)desk.host.work_height)
        desk.scroll_y+=r.y+r.height-(ScI32)desk.host.work_height;
    if(desk.scroll_y<0)desk.scroll_y=0;
    if(desk.scroll_x<0)desk.scroll_x=0;
    if(old!=desk.scroll_y || old_x!=desk.scroll_x)ScUiInvalidate(desk.ui,desk.canvas,0);
}
static void desk_gesture_cancel(void)
{
    if(!desk.gesture)return;
    for(ScU32 i=0;i<desk.count;i++)desk.items[i].selected=desk.items[i].old_selected;
    desk.gesture=desk.moved=0;ScUiCapture(desk.ui,desk.canvas,0);ScUiInvalidate(desk.ui,desk.canvas,0);
}
static void desk_range(ScU32 hit,ScU32 modifiers)
{
    if(hit>=desk.count)return;
    if((modifiers&SC32_MOD_SHIFT) && desk.anchor<desk.count){
        ScU32 first=desk.anchor<hit?desk.anchor:hit,last=desk.anchor>hit?desk.anchor:hit;
        for(ScU32 i=0;i<desk.count;i++)desk_select(i,(i>=first && i<=last)
            || ((modifiers&SC32_MOD_CONTROL) && desk.items[i].selected));
    }else if(modifiers&SC32_MOD_CONTROL){desk_select(hit,!desk.items[hit].selected);desk.anchor=hit;}
    else {desk_select_only(hit);desk.anchor=hit;}
    desk_focus_item(hit);
}
static void desk_box_update(const ScUiRect *old)
{
    ScUiRect bounds=desk.rubber;
    ScI32 right=old->x+old->width,bottom=old->y+old->height;
    if(right<bounds.x+bounds.width)right=bounds.x+bounds.width;
    if(bottom<bounds.y+bounds.height)bottom=bounds.y+bounds.height;
    if(old->width && old->x<bounds.x)bounds.x=old->x;
    if(old->height && old->y<bounds.y)bounds.y=old->y;
    bounds.width=right-bounds.x;bounds.height=bottom-bounds.y;
    ScI32 scale=(ScI32)desk.metrics.scale_percent;
    ScI32 x0=desk_floor((bounds.x+desk.scroll_x)*100/scale,(ScI32)desk.grid_w)-1;
    ScI32 x1=desk_floor((bounds.x+bounds.width+desk.scroll_x)*100/scale,(ScI32)desk.grid_w);
    ScI32 y0=desk_floor((bounds.y+desk.scroll_y)*100/scale,(ScI32)desk.grid_h)-1;
    ScI32 y1=desk_floor((bounds.y+bounds.height+desk.scroll_y)*100/scale,(ScI32)desk.grid_h);
    for(ScI32 y=y0;y<=y1;y++)for(ScI32 x=x0;x<=x1;x++){
        DeskCell *cell=desk.cell_capacity?desk_cell(x,y):0;if(!cell || !cell->used)continue;
        for(ScU32 i=cell->first;i!=DESK_NONE;i=desk.items[i].next_cell){
            ScUiRect r=desk_item_rect(i);ScU32 inside=desk_intersects(&r,&desk.rubber);
            ScU32 selected=(desk.selection_modifiers&SC32_MOD_CONTROL)?desk.items[i].old_selected^inside:
                (desk.selection_modifiers&SC32_MOD_SHIFT)?desk.items[i].old_selected|inside:inside;
            desk_select(i,selected);
        }
    }
}
static ScStatus desk_input(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)context;
    if(event->type==SC_UI_EVENT_DROP && event->data && event->bytes==sizeof(ScUiDragData)){
        const ScUiDragData *data=(const ScUiDragData *)event->data;
        if(data->phase==SC32_DRAG_COMPLETE){desk.external_drag=0;return 0;}
    }
    if(event->type==SC_UI_EVENT_CAPTURE_LOST || event->type==SC_UI_EVENT_INPUT_SYNC){desk_gesture_cancel();return 0;}
    if(event->type==SC_UI_EVENT_KEY_DOWN){ScU32 key=event->key,mod=event->modifiers;
        if(key==27 && (mod&SC32_MOD_CONTROL)){desk_error(desk_menu(SC_SHELL_MENU_START,0,0),"");return SC_UI_HANDLED;}
        if(key==SC32_KEY_F1+5){ScUiFocus(ui,desk.task_canvas);return SC_UI_HANDLED;}
        if(key==SC32_KEY_F1+1)return desk_command(SC_SHELL_COMMAND_RENAME);
        if(key==SC32_KEY_F1+9 && (mod&SC32_MOD_SHIFT)){
            ScUiRect r={0,0,0,0};if(desk.focus<desk.count)r=desk_item_rect(desk.focus);
            desk_error(desk_menu(desk.focus<desk.count?SC_SHELL_MENU_SELECTION:SC_SHELL_MENU_BACKGROUND,r.x,r.y),"");return SC_UI_HANDLED;
        }
        if((mod&(SC32_MOD_CONTROL|SC32_MOD_SHIFT))==(SC32_MOD_CONTROL|SC32_MOD_SHIFT) && (key=='n' || key=='N'))
            return desk_command(SC_SHELL_COMMAND_NEW_DIRECTORY);
        if(key==27){desk_gesture_cancel();return SC_UI_HANDLED;}
        if((key=='\n' || key==13) && desk.focus<desk.count){desk_error(desk_open(desk.focus),desk.items[desk.focus].path);return SC_UI_HANDLED;}
        if((mod&SC32_MOD_CONTROL) && (key=='a' || key=='A'))return desk_command(DESK_SELECT_ALL);
        if((mod&SC32_MOD_CONTROL) && (key=='c' || key=='C'))return desk_command(SC_SHELL_COMMAND_COPY);
        if((mod&SC32_MOD_CONTROL) && (key=='x' || key=='X'))return desk_command(SC_SHELL_COMMAND_CUT);
        if((mod&SC32_MOD_CONTROL) && (key=='v' || key=='V'))return desk_command(SC_SHELL_COMMAND_PASTE);
        if(key==SC32_KEY_F1+4)return desk_command(DESK_REFRESH);
        if(key==SC32_KEY_DELETE)return desk_command(SC_SHELL_COMMAND_DELETE);
        if(desk.count && (key==SC32_KEY_LEFT || key==SC32_KEY_RIGHT || key==SC32_KEY_UP || key==SC32_KEY_DOWN)){
            ScU32 at=desk.focus<desk.count?desk.focus:0;ScUiRect from=desk_item_rect(at);
            ScU32 best=DESK_NONE,distance=0xffffffffu;
            for(ScU32 i=0;i<desk.count;i++)if(i!=at && !desk.items[i].removed){ScUiRect to=desk_item_rect(i);
                ScI32 dx=to.x-from.x,dy=to.y-from.y;
                if((key==SC32_KEY_LEFT && dx>=0) || (key==SC32_KEY_RIGHT && dx<=0)
                    || (key==SC32_KEY_UP && dy>=0) || (key==SC32_KEY_DOWN && dy<=0))continue;
                ScU32 primary=(ScU32)((key==SC32_KEY_LEFT || key==SC32_KEY_RIGHT)?abs(dx):abs(dy));
                ScU32 cross=(ScU32)((key==SC32_KEY_LEFT || key==SC32_KEY_RIGHT)?abs(dy):abs(dx));
                ScU32 metric=primary+cross*4u;if(metric<distance){distance=metric;best=i;}
            }
            if(best!=DESK_NONE)desk_range(best,mod);
            return SC_UI_HANDLED;
        }
    }
    if(event->type==SC_UI_EVENT_WHEEL && event->message){
        if(desk.gesture)return SC_UI_HANDLED;
        ScI32 horizontal=(ScI32)event->message->data[3];ScI32 delta=(ScI32)event->message->data[2];
        int across=horizontal!=0 || (event->modifiers&SC32_MOD_SHIFT);
        if(horizontal)delta=horizontal;
        ScI32 maximum=(across?desk.extent_w-(ScI32)desk.host.work_width:desk.extent_h-(ScI32)desk.host.work_height);
        if(maximum<0)maximum=0;
        ScI32 previous=across?desk.scroll_x:desk.scroll_y;
        ScI32 next=previous-delta*desk_scale(32);if(next<0)next=0;if(next>maximum)next=maximum;
        if(next!=previous){if(across)desk.scroll_x=next;else desk.scroll_y=next;ScUiInvalidate(ui,desk.canvas,0);}return SC_UI_HANDLED;
    }
    if(event->type==SC_UI_EVENT_POINTER_DOWN){
        if(desk.external_drag){ScUiDragCancel(ui,desk.external_drag);desk.external_drag=0;}
        ScU32 changed=event->message?event->message->data[3]:0;
        ScU32 hit=desk_hit(event->x,event->y);
        if((changed&2u) && (event->buttons&2u)){if(hit!=DESK_NONE && !desk.items[hit].selected)desk_range(hit,0);
            desk_error(desk_menu(hit==DESK_NONE?SC_SHELL_MENU_BACKGROUND:SC_SHELL_MENU_SELECTION,event->x,event->y),"");
            return SC_UI_HANDLED;}
        if(!(changed&1u) || !(event->buttons&1u))return 0;
        for(ScU32 i=0;i<desk.count;i++){desk.items[i].old_selected=desk.items[i].selected;
            desk.items[i].old_x=desk.items[i].x;desk.items[i].old_y=desk.items[i].y;}
        if(hit!=DESK_NONE){if(!desk.items[hit].selected || event->modifiers&(SC32_MOD_SHIFT|SC32_MOD_CONTROL))desk_range(hit,event->modifiers);
            else desk_focus_item(hit);
            desk.gesture=DESK_DRAG;
            desk.drag_count=0;
            for(ScU32 i=0;i<desk.count;i++)if(desk.items[i].selected){
                if(!desk_reserve((void **)&desk.drag_items,&desk.drag_capacity,desk.drag_count+1,sizeof(ScU32))){
                    desk_gesture_cancel();return SC_UI_E_MEMORY;}
                desk.drag_items[desk.drag_count++]=i;
            }}
        else {if(!(event->modifiers&(SC32_MOD_SHIFT|SC32_MOD_CONTROL)))desk_select_only(DESK_NONE);desk.gesture=DESK_BOX;}
        desk.down_x=desk.pointer_x=event->x;desk.down_y=desk.pointer_y=event->y;
        desk.rubber.x=event->x;desk.rubber.y=event->y;desk.rubber.width=desk.rubber.height=0;
        desk.pressed=hit;desk.moved=0;desk.selection_modifiers=event->modifiers;
        desk.save_epoch=desk.layout_epoch;
        ScStatus s=ScUiCapture(ui,desk.canvas,1);if(s<0){desk_gesture_cancel();return s;}
        ScUiFocus(ui,desk.canvas);return SC_UI_HANDLED;
    }
    if(event->type==SC_UI_EVENT_POINTER_MOVE && desk.gesture){
        ScI32 dx=event->x-desk.down_x,dy=event->y-desk.down_y;
        if(!desk.moved && abs(dx)+abs(dy)<desk_scale(5))return SC_UI_HANDLED;
        if(desk.gesture==DESK_DRAG){
            ScWindowInfo own,top;memset(&own,0,sizeof(own));own.size=sizeof(own);own.version=1;
            memset(&top,0,sizeof(top));top.size=sizeof(top);top.version=1;
            ScStatus queried=ScUiWindowNative(ui,desk.desktop,&own);
            if(queried==0){ScShellHostPoint point={sizeof(point),1,desk.host.host_epoch,
                    own.x+event->x,own.y+event->y,0,{0,0}};
                queried=ScShellHostWindowAt(&point,&top);}
            if(queried==0 && (top.window!=own.window || (event->modifiers&SC32_MOD_ALT))){
                ScStatus started=desk_begin_transfer();desk_error(started,"");return started<0?started:SC_UI_HANDLED;
            }
            if(queried<0 && queried!=SC32_E_NOT_FOUND){desk_gesture_cancel();return queried;}
        }
        if(desk.gesture==DESK_DRAG){
            for(ScU32 i=0;i<desk.drag_count;i++)desk_invalidate_item(desk.drag_items[i]);
            desk.pointer_x=event->x;desk.pointer_y=event->y;desk.moved=1;
            for(ScU32 i=0;i<desk.drag_count;i++)desk_invalidate_item(desk.drag_items[i]);
        }else{
            ScUiRect old=desk.rubber;
            ScUiInvalidate(ui,desk.canvas,&desk.rubber);
            desk.pointer_x=event->x;desk.pointer_y=event->y;desk.moved=1;
            desk.rubber.x=dx<0?event->x:desk.down_x;desk.rubber.y=dy<0?event->y:desk.down_y;
            desk.rubber.width=abs(dx)+1;desk.rubber.height=abs(dy)+1;
            desk_box_update(&old);
            ScUiInvalidate(ui,desk.canvas,&desk.rubber);
        }
        return SC_UI_HANDLED;
    }
    if(event->type==SC_UI_EVENT_POINTER_UP && desk.gesture){
        if(!event->message || !(event->message->data[3]&1u))return 0;
        ScU32 kind=desk.gesture,moved=desk.moved,hit=desk_hit(event->x,event->y);desk.gesture=0;
        ScUiCapture(ui,desk.canvas,0);
        if(kind==DESK_DRAG && moved){
            ScU32 previous_mode=desk.layout_mode;
            ScI32 dx=(event->x-desk.down_x)*100/(ScI32)desk.metrics.scale_percent;
            ScI32 dy=(event->y-desk.down_y)*100/(ScI32)desk.metrics.scale_percent;
            if(desk.layout_mode==SC_SHELL_LAYOUT_AUTO)desk.layout_mode=SC_SHELL_LAYOUT_FREE;
            if(desk.layout_mode==SC_SHELL_LAYOUT_GRID){
                dx=(dx+(dx<0?-(ScI32)desk.grid_w/2:(ScI32)desk.grid_w/2))/(ScI32)desk.grid_w*(ScI32)desk.grid_w;
                dy=(dy+(dy<0?-(ScI32)desk.grid_h/2:(ScI32)desk.grid_h/2))/(ScI32)desk.grid_h*(ScI32)desk.grid_h;
            }
            ScI32 min_x=1048576,min_y=1048576;
            for(ScU32 at=0;at<desk.drag_count;at++){DeskItem *item=desk.items+desk.drag_items[at];
                if(item->old_x<min_x)min_x=item->old_x;
                if(item->old_y<min_y)min_y=item->old_y;}
            if(dx< -min_x)dx= -min_x;
            if(dy< -min_y)dy= -min_y;
            for(ScU32 at=0;at<desk.drag_count;at++){ScU32 i=desk.drag_items[at];desk.items[i].x=desk.items[i].old_x+dx;desk.items[i].y=desk.items[i].old_y+dy;
            }
            ScStatus indexed=desk_index();
            if(indexed<0){for(ScU32 at=0;at<desk.drag_count;at++){DeskItem *item=desk.items+desk.drag_items[at];
                    item->x=item->old_x;item->y=item->old_y;}
                desk.layout_mode=previous_mode;desk_error(indexed,desk.store_path);}
            else desk_layout_changed();
            ScUiInvalidate(ui,desk.canvas,0);
        }else if(kind==DESK_BOX)ScUiInvalidate(ui,desk.canvas,&desk.rubber);
        else if(hit!=DESK_NONE && hit==desk.pressed){
            if(desk.last_click==hit && event->tick-desk.last_click_tick<=40u){desk.last_click=DESK_NONE;
                desk_error(desk_open(hit),desk.items[hit].path);}
            else {desk.last_click=hit;desk.last_click_tick=event->tick;}
        }
        desk.moved=0;return SC_UI_HANDLED;
    }
    return 0;
}
