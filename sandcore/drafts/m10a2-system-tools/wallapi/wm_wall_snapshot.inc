/* 放入kernel/wm.c，view_get及真实desktop_view_t在同一可信TU内。
* 内核头wm.h同批声明此函数；应用只能取得SC32WALL的值快照。 */
int wm_wallpaper_snapshot(int pid,u32 *mode,u32 *session)
{
    if(pid<=0 || pid!=task_pid() || !task_owned(pid) || !mode || !session)return SC32_E_INVALID;
    u32 sid=session_task(pid);
    session_t *subject=session_get(sid);
    if(!subject || subject->closing || subject->kind==SESSION_LOCKED)return SC32_E_ACCESS;
    desktop_view_t *view=view_get(sid);
    if(!view)return SC32_E_NOT_FOUND;
    if(view->wallpaper<0 || view->wallpaper>2)return SC32_E_FAILED;
    *mode=(u32)view->wallpaper;
    *session=sid;
    return 0;
}
