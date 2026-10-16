/* 放入kernel/sc32_core.c，其clear/publish和caller_valid保持同一
* 原子IF=0调用。另包含wm.h、theme.h、SC32WALL.H与私有WIRE头。 */
static int wall_mode_query(int pid,u32 request,u32 output)
{
    if(request)return SC32_E_INVALID;
    int status=sc32_core_output(pid,output,sizeof(ScWallpaperInfo));
    if(status)return status;
    ScWallpaperInfo info;
    clear(&info,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    status=wm_wallpaper_snapshot(pid,&info.mode,&info.session);
    if(status)return status;
    char selected_path[64];
    int selected=theme_path(0,selected_path,sizeof(selected_path));
    if(selected<0)return SC32_E_FAILED;
    if(selected)info.flags|=SC32_WALL_IMAGE_SELECTED;
    return publish(pid,output,&info,sizeof(info));
}
/* sc32_core_call switch添加：
* case SC32_W_QUERY:return wall_mode_query(pid,request,output);
* task.c的现有default已进入core，无需为此引入新的业务分发组。 */
