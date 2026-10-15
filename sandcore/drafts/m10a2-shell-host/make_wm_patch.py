"""仅写隔离WM接线diff；不改工作源、不构建、不启动客体。"""
from pathlib import Path
import difflib
import hashlib
import json

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
changes={}


def replace(path,old,new,count=1):
    before=changes.get(path,(ROOT/path).read_text(encoding='utf-8'))
    if before.count(old)!=count:
        raise ValueError('WM接线锚点不唯一/源漂移：'+path+' '+repr(old[:80]))
    changes[path]=before.replace(old,new,count)


replace('kernel/wm.c','#include "sc32_drop.h"','#include "sc32_drop.h"\n#include "sc32_host.h"')
replace('kernel/wm.c','    int z_index,creation_index;','''    int z_index,creation_index;
    u32 shell_role,shell_ancestor,shell_submitted_geometry,shell_submitted_paint,shell_ready_geometry;''')
replace('kernel/wm.c','    int top_cache,top_known;','''    int top_cache,top_known;
    u32 shell_session,shell_pid,shell_generation,shell_desktop,shell_taskbar;
    u32 shell_height,shell_prepared,shell_active,shell_focus,shell_windows_epoch;''')
replace('kernel/wm.c','static desktop_view_t *active_view;','''static desktop_view_t *active_view;
static int wm_shell_preparing(window_record_t *p);
static int wm_shell_ancestor_visible(window_record_t *p);
static int wm_shell_drawable(window_record_t *p);
static int wm_shell_work_bottom_for(u32 sid);
static void wm_shell_changed(u32 sid);
static int wm_shell_raise_destination(desktop_view_t *view,window_record_t *p);
static void wm_shell_focus_set(window_record_t *p);
static void wm_shell_created(desktop_view_t *view,window_record_t *p);
static int wm_shell_top(desktop_view_t *view);
static int wm_shell_mouse(int x,int y,u8 buttons,u8 previous);
static int wm_shell_draw_surface(window_record_t *p);
static void wm_shell_frame_invalidated(window_record_t *p);
static void wm_shell_submitted(window_record_t *p,const ScSubmitDesc *input,const ScSubmitRect *rectangles);
static void wm_shell_paint_complete(window_record_t *p,const Sc32PaintEnd *input);
static void wm_shell_paint_cancelled(window_record_t *p);
static void wm_shell_closing(window_record_t *p);
static void wm_shell_display(desktop_view_t *view);''')
replace('kernel/wm.c','return p && p->gui.popup?0:1;','return p && (p->gui.popup || p->shell_role)?0:1;')
replace('kernel/wm.c','return p && p->gui.popup?0:TITLE_H;','return p && (p->gui.popup || p->shell_role)?0:TITLE_H;')
replace('kernel/wm.c','    int result=-1;for(int i=nwins-1;i>=0;i--){','''    if(wm_shell_active(session_active())){
        int result=wm_shell_top(active_view);
        active_view->top_cache=result;active_view->top_known=1;return result;
    }
    int result=-1;for(int i=nwins-1;i>=0;i--){''')
replace('kernel/wm.c','static int work_bottom(void){return GFX_H-(GFX_W==320?16:32*display_scale()/100);}',
    'static int work_bottom(void){return wm_shell_work_bottom_for(session_active());}')
replace('kernel/wm.c','{wm_damage_rect(0,work_bottom(),GFX_W,GFX_H-work_bottom());}',
    '{if(!wm_shell_active(session_active()))wm_damage_rect(0,work_bottom(),GFX_W,GFX_H-work_bottom());}')
replace('kernel/wm.c','np->y=(work_bottom()-np->h)/2;','np->y=(wm_shell_work_bottom_for(id)-np->h)/2;')
replace('kernel/wm.c','    view->count++;\n    view->top_known=0;','''    view->count++;
    record->gui.geometry=record->gui.input=record->gui.theme=record->gui.display=1;
    record->gui.last_x=np->x;record->gui.last_y=np->y;
    record->gui.last_w=np->w;record->gui.last_h=np->h;
    record->gui.last_cw=np->cw;record->gui.last_ch=np->ch;
    wm_shell_created(view,record);
    view->top_known=0;''')
replace('kernel/wm.c','    desktop_view_t *view=view_get(record->session);if(!view)return;\n    /* 关闭一个终端',
    '    desktop_view_t *view=view_get(record->session);if(!view)return;\n    wm_shell_closing(record);\n    /* 关闭一个终端')
replace('kernel/wm.c','    view->count--;\n    view->top_known=0;',
    '    view->count--;\n    wm_shell_changed(record->session);\n    view->top_known=0;')
replace('kernel/wm.c','static void win_raise(int idx)\n{\n    if(idx<0 || idx>=nwins)return;',
    'static int win_raise(int idx)\n{\n    if(idx<0 || idx>=nwins)return -1;')
replace('kernel/wm.c','    for (int i = idx; i < nwins - 1; i++) {','''    window_record_t *raised=window_record(handle);
    int destination=wm_shell_raise_destination(active_view,raised);
    for (int i = idx; i < destination; i++) {''')
replace('kernel/wm.c','    window_order[nwins-1]=handle;window_record(handle)->z_index=nwins-1;',
    '    window_order[destination]=handle;window_record(handle)->z_index=destination;\n    wm_shell_focus_set(raised);')
replace('kernel/wm.c','    task_notify(window_record(handle)->window.owner,TASK_EVENT_INPUT);\n    gui_focus_update();\n}',
    '''    task_notify(window_record(handle)->window.owner,TASK_EVENT_INPUT);
    gui_focus_update();
    raised=window_record(handle);return raised?raised->z_index:-1;
}''')
replace('kernel/wm.c','    if(p->gui.mode){p->gui.theme++;ScMessage m={0};',
    '    if(p->gui.mode){wm_shell_frame_invalidated(p);p->gui.theme++;ScMessage m={0};')
replace('kernel/wm.c','    if(GFX_W!=320 || window_record(w->handle)->gui.popup){native_draw_window(i);return;}',
    '    if(wm_shell_draw_surface(window_record(w->handle)))return;\n    if(GFX_W!=320 || window_record(w->handle)->gui.popup){native_draw_window(i);return;}')
replace('kernel/wm.c','''        if(GFX_W==320 || !desktop_wallpaper())scene_paint();
        draw_icons();
        for(int i=0;i<nwins;i++)draw_window(i);
        draw_taskbar();native_draw_overlays();draw_fault();''','''        if(wm_shell_active(session_active())){
            /* 壁纸仍由内核原有单一路径缓存；R3透明层只负责图标内容。 */
            if(GFX_W==320 || !desktop_wallpaper())scene_paint();
        }else{
            if(GFX_W==320 || !desktop_wallpaper())scene_paint();
            draw_icons();
        }
        for(int i=0;i<nwins;i++)draw_window(i);
        if(!wm_shell_active(session_active()))draw_taskbar();
        native_draw_overlays();draw_fault();''')
replace('kernel/wm.c','    if(terminal_mouse(x,y,b,pbtn)){',
    '''    if(wm_shell_mouse(x,y,b,pbtn)){
        cursor_dirty=1;pmx=x;pmy=y;pbtn=b;return;
    }
    if(terminal_mouse(x,y,b,pbtn)){''')
replace('kernel/wm.c','''            win_raise(w);
            w = nwins - 1;''','''            w=win_raise(w);
            if(w<0){pmx=x;pmy=y;pbtn=b;return;}''')
replace('kernel/wm.c','    view->display_width=GFX_W;view->display_height=GFX_H;view->display_scaling=display_scale();',
    '    view->shell_session=id;view->shell_windows_epoch=1;\n    view->display_width=GFX_W;view->display_height=GFX_H;view->display_scaling=display_scale();')
replace('kernel/wm.c','return p && !p->extra.hidden && (!p->gui.mode || (!p->gui.creating && !p->gui.owner_hidden));',
    'return p && task_owned(p->window.owner) && TASK(p->window.owner).state!=2\n    && (!p->shell_role || wm_shell_active(p->session)) && wm_shell_ancestor_visible(p)\n    && !p->extra.hidden && (!p->gui.mode || (!p->gui.creating && !p->gui.owner_hidden));')
replace('kernel/wm.c','    gui_window_t *g=&p->gui;g->parent=parent?(u32)parent->window.handle:0;',
    '    gui_window_t *g=&p->gui;g->parent=parent?(u32)parent->window.handle:0;\n    p->shell_ancestor=parent?(parent->shell_role?(u32)parent->window.handle:parent->shell_ancestor):0;')
replace('kernel/wm.c','    g->parent=g->previous_child=g->next_child=0;',
    '    g->parent=g->previous_child=g->next_child=0;p->shell_ancestor=0;')
replace('kernel/wm.c','        p->gui.owner_hidden=parent->gui.owner_hidden || parent->extra.hidden || parent->gui.creating;',
    '        p->shell_ancestor=parent->shell_role?(u32)parent->window.handle:parent->shell_ancestor;\n        p->gui.owner_hidden=parent->gui.owner_hidden || parent->extra.hidden || parent->gui.creating;')
replace('kernel/wm.c','        if(parent->gui.popup)return SC32_E_NOT_SUPPORTED;',
    '        if(parent->gui.popup)return SC32_E_NOT_SUPPORTED;\n        if(parent->shell_role && !wm_shell_active(parent->session))return SC32_E_AGAIN;',count=2)
replace('kernel/wm.c','    gui_focus_any=handle;','''    gui_focus_any=handle;
    if(old)wm_shell_changed(old->session);
    if(next && (!old || next->session!=old->session))wm_shell_changed(next->session);''')
replace('kernel/wm.c','''void wm_sc32_owner_stopped(int pid)
{
    /* 退出先撤唯一capture''','''void wm_sc32_owner_stopped(int pid)
{
    desktop_view_t *stopped_view=view_get(session_task(pid));
    if(stopped_view){stopped_view->top_known=0;wm_shell_changed(session_task(pid));}
    /* 退出先撤唯一capture''')
replace('kernel/wm.c','''    if(!p || !p->gui.mode || gui_mutation || TASK(w->owner).state==2)return;
    gui_window_t *g=&p->gui;int visible=window_visible(w);''','''    if(!p || gui_mutation || TASK(w->owner).state==2)return;
    gui_window_t *g=&p->gui;int visible=window_visible(w);
    int dimensions=g->last_cw!=w->cw || g->last_ch!=w->ch || g->last_w!=w->w || g->last_h!=w->h;
    if(dimensions || g->last_x!=w->x || g->last_y!=w->y || g->last_visible!=visible)wm_shell_changed(p->session);
    if(!g->mode){
        if(dimensions && ++g->geometry==0)g->geometry=1;
        g->last_x=w->x;g->last_y=w->y;g->last_w=w->w;g->last_h=w->h;
        g->last_cw=w->cw;g->last_ch=w->ch;g->last_visible=visible;return;
    }''')
replace('kernel/wm.c','        g->geometry++;if(!g->geometry)g->geometry=1;g->painting=0;g->input++;',
    '        g->geometry++;if(!g->geometry)g->geometry=1;g->painting=0;g->input++;\n        wm_shell_frame_invalidated(p);')
replace('kernel/wm.c','    if(height>work_bottom())height=work_bottom();','    if(height>wm_shell_work_bottom_for(session_task(pid)))height=wm_shell_work_bottom_for(session_task(pid));')
replace('kernel/wm.c','    if(y>work_bottom()-height)y=work_bottom()-height;',
    '    if(y>wm_shell_work_bottom_for(session_task(pid))-height)y=wm_shell_work_bottom_for(session_task(pid))-height;')
replace('kernel/wm.c','        |(p->gui.popup?SC32_WINDOW_POPUP:0)',
    '        |(p->session==session_active() && wm_shell_preparing(p)?SC32_WINDOW_PREPARING:0)\n        |(p->gui.popup?SC32_WINDOW_POPUP:0)')
replace('kernel/wm.c','    if(p->gui.creating)return SC32_E_BUSY;\n    if(input->width<',
    '    if(p->shell_role)return SC32_E_NOT_SUPPORTED;\n    if(p->gui.creating)return SC32_E_BUSY;\n    if(input->width<')
# control也需保留真实FOCUS；另外一处creating锚点在place替换后仍唯一。
replace('kernel/wm.c','''    if(p->gui.creating)return SC32_E_BUSY;
    if(input->action>SC32_WINDOW_FOCUS)''','''    if(p->shell_role && input->action!=SC32_WINDOW_FOCUS)return SC32_E_NOT_SUPPORTED;
    if(p->gui.creating)return SC32_E_BUSY;
    if(input->action>SC32_WINDOW_FOCUS)''')
replace('kernel/wm.c','    if(p->gui.popup)return SC32_E_NOT_SUPPORTED;',
    '    if(p->gui.popup || p->shell_role)return SC32_E_NOT_SUPPORTED;')
replace('kernel/wm.c','    p->window.title[n]=0;window_changed(&p->window);return SC32_OK;',
    '    p->window.title[n]=0;wm_shell_changed(p->session);window_changed(&p->window);return SC32_OK;')
replace('kernel/wm.c','    sc32_gui_stamp(w->owner,m);gui_attention(p);sc32_gui_notify(w->owner);',
    '    sc32_gui_stamp(w->owner,m);gui_attention(p);wm_shell_changed(p->session);sc32_gui_notify(w->owner);')
replace('kernel/wm.c','    p->gui.close_pending=p->gui.close_ready=0;gui_attention(p);return SC32_OK;',
    '    int changed=p->gui.close_pending || p->gui.close_ready;\n    p->gui.close_pending=p->gui.close_ready=0;gui_attention(p);\n    if(changed)wm_shell_changed(p->session);\n    return SC32_OK;')
replace('kernel/wm.c','    if(!window_visible(&p->window) || g->creating){',
    '    if(!wm_shell_drawable(p) || g->creating){')
replace('kernel/wm.c','gui_attention(p);if(window_visible(&p->window))sc32_gui_notify(pid);return SC32_OK;',
    'gui_attention(p);if(wm_shell_drawable(p))sc32_gui_notify(pid);return SC32_OK;')
replace('kernel/wm.c','    if(!window_visible(&p->window))return SC32_E_AGAIN;',
    '    if(!wm_shell_drawable(p))return SC32_E_AGAIN;',count=2)
replace('kernel/wm.c','    else if(!g->paint_ready && window_visible(&p->window)){',
    '    else if(!g->paint_ready && wm_shell_drawable(p)){')
replace('kernel/wm.c','''    gui_attention(p);if(g->paint_ready)sc32_gui_notify(pid);return SC32_OK;
}
int wm_sc32_paint_cancel''','''    wm_shell_paint_complete(p,input);
    gui_attention(p);if(g->paint_ready)sc32_gui_notify(pid);return SC32_OK;
}
int wm_sc32_paint_cancel''')
replace('kernel/wm.c','''int wm_sc32_paint_end(int pid,const Sc32PaintEnd *input)
{
    int error;window_record_t *p=gui_record(pid,input->window,input->generation,&error);if(!p)return error;
    gui_window_t *g=&p->gui;''','''int wm_sc32_paint_end(int pid,const Sc32PaintEnd *input)
{
    int error;window_record_t *p=gui_record(pid,input->window,input->generation,&error);if(!p)return error;
    if(!wm_shell_drawable(p))return SC32_E_STALE;
    gui_window_t *g=&p->gui;''')
replace('kernel/wm.c','    g->painting=g->painting_geometry=0;','    wm_shell_paint_cancelled(p);\n    g->painting=g->painting_geometry=0;')
replace('kernel/wm.c','    if(g->paint_pending && !g->paint_ready && window_visible(&p->window) && !g->creating){',
    '    if(g->paint_pending && !g->paint_ready && wm_shell_drawable(p) && !g->creating){')
replace('kernel/wm.c','''    if(changed)wm_damage_rect(p->window.x+window_client_dx(&p->window)+x0,p->window.y+window_client_dy(&p->window)+y0,x1-x0,y1-y0);
    return SC32_OK;''','''    wm_shell_submitted(p,input,rectangles);
    if(changed && window_visible(&p->window))wm_damage_rect(p->window.x+window_client_dx(&p->window)+x0,p->window.y+window_client_dy(&p->window)+y0,x1-x0,y1-y0);
    return SC32_OK;''')
replace('kernel/wm.c','p->gui.paint_ready && window_visible(&p->window) && !p->gui.creating',
    'p->gui.paint_ready && wm_shell_drawable(p) && !p->gui.creating',count=2)
replace('kernel/wm.c','        || event->y>=work_bottom() || relative_active();',
    '        || (event->y>=work_bottom() && !wm_shell_active(session_active())) || relative_active();')
replace('kernel/wm.c','    if(original && !original->gui.popup && (event->x<=original->window.x',
    '    if(original && !original->gui.popup && !original->shell_role && (event->x<=original->window.x')
changes['kernel/wm.c']+='\n#include "wm_shell.inc"\n'

replace('kernel/wm_native.inc','    if(width>GFX_W)width=GFX_W;\n    if(height>work_bottom())height=work_bottom();\n    if(width<96)',
    '    if(width>GFX_W)width=GFX_W;\n    if(height>wm_shell_work_bottom_for(window_record(w->handle)->session))height=wm_shell_work_bottom_for(window_record(w->handle)->session);\n    if(width<96)')
replace('kernel/wm_native.inc','    if(w->y+w->h>work_bottom())w->y=work_bottom()-w->h;',
    '    if(w->y+w->h>wm_shell_work_bottom_for(window_record(w->handle)->session))w->y=wm_shell_work_bottom_for(window_record(w->handle)->session)-w->h;')
replace('kernel/wm_native.inc','    popup_close_group(active_view,0,0);\n    active_view->display_width=',
    '    popup_close_group(active_view,0,0);\n    wm_shell_display(active_view);\n    active_view->display_width=')
replace('kernel/wm_native.inc','        window_record_t *p=window_record(WINDOW(i).handle);p->gui.display++;',
    '        window_record_t *p=window_record(WINDOW(i).handle);wm_shell_frame_invalidated(p);p->gui.display++;')
replace('kernel/wm_native.inc','        window_extra_t *e=extra_of(w->handle);\n        if(!e->native)',
    '        window_extra_t *e=extra_of(w->handle);\n        if(window_record(w->handle)->shell_role)continue;\n        if(!e->native)')
replace('kernel/wm_native.inc','    if(height>work_bottom())height=work_bottom();\n    int result=win_create(pid,title,(GFX_W-width)/2,(work_bottom()-height)/2,width,height,',
    '    if(height>wm_shell_work_bottom_for(session_task(pid)))height=wm_shell_work_bottom_for(session_task(pid));\n    int result=win_create(pid,title,(GFX_W-width)/2,(wm_shell_work_bottom_for(session_task(pid))-height)/2,width,height,',count=2)
replace('kernel/wm_native.inc','    if(!w || !e || !e->native || width<94',
    '    if(!w || !e || window_record(handle)->shell_role || !e->native || width<94')
replace('kernel/wm_native.inc','    if(y>work_bottom()-w->h)y=work_bottom()-w->h;',
    '    if(y>wm_shell_work_bottom_for(window_record(w->handle)->session)-w->h)y=wm_shell_work_bottom_for(window_record(w->handle)->session)-w->h;')
replace('kernel/wm_native.inc','    if(!w)return -1;\n    window_extra_t *e=extra_of(handle);\n    if(command<0',
    '    if(!w || window_record(handle)->shell_role)return -1;\n    window_extra_t *e=extra_of(handle);\n    if(command<0')
replace('kernel/wm_native.inc','        if(native_resize(w,GFX_W,work_bottom()))',
    '        if(native_resize(w,GFX_W,wm_shell_work_bottom_for(window_record(handle)->session)))')
replace('kernel/wm_native.inc','        win_raise(i);\n        i=nwins-1;',
    '        i=win_raise(i);\n        if(i<0)return 1;')
replace('user/SC32GUI.H','#define SC32_WINDOW_CAPTURED 512u',
    '#define SC32_WINDOW_CAPTURED 512u\n#define SC32_WINDOW_PREPARING 8192u')

patch=[]
metadata=dict(status='ISOLATED_WM_SOURCE_NOT_BUILT_NOT_RUN',inputs={},changed_paths=sorted(changes),
    new_source='kernel/wm_shell.inc',new_source_in_parent_integration=True,
    working_sources_modified=False,full_M10a2=False)
for path,after in changes.items():
    raw=(ROOT/path).read_bytes();before=raw.decode('utf-8')
    old_lines=before.splitlines(True);normalized=[s.replace('\r\n','\n') for s in old_lines]
    new_lines=after.splitlines(True);preserved=[]
    for kind,a0,a1,b0,b1 in difflib.SequenceMatcher(a=normalized,b=new_lines,autojunk=False).get_opcodes():
        if kind=='equal':preserved.extend(old_lines[a0:a1])
        else:
            anchor=old_lines[min(a0,len(old_lines)-1)] if old_lines else '\n'
            ending='\r\n' if anchor.endswith('\r\n') else '\n'
            preserved.extend(s[:-1]+ending if s.endswith('\n') else s for s in new_lines[b0:b1])
    actual=''.join(preserved)
    metadata['inputs'][path]=hashlib.sha256(raw).hexdigest()
    metadata.setdefault('candidate_sha256',{})[path]=hashlib.sha256(actual.encode('utf-8')).hexdigest()
    patch.extend(difflib.unified_diff(before.splitlines(True),actual.splitlines(True),
        'a/sandcore/'+path,'b/sandcore/'+path))
source=HERE/'kernel/wm_shell.inc'
metadata['new_source_sha256']=hashlib.sha256(source.read_bytes()).hexdigest()
(HERE/'integration-wm.patch').write_text(''.join(patch),encoding='utf-8',newline='\n')
(HERE/'wm-source-inputs.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('ISOLATED_WM_PATCH_WRITTEN working_sources_modified=false')
