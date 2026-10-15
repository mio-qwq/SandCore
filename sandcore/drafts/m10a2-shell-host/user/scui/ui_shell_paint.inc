/* Phase5新增正式Painter入口。透明表示本surface没有内容，不是新的
 * RGB调色板色；让R3桌面图标层复用既有壁纸合成，不再复制整幅背景。 */
ScStatus ScUiPainterClear(ScUiPainter *p,const ScUiRect *local)
{
    ScUiRect area;
    if(!p || !local)return SC_UI_E_INVALID;
    if(!ui_painter_rect(p,local,&area))return p->error;
    for(ScI32 y=0;y<area.height;y++){
        ScU32 *row=p->root->pixels+(ScU32)(area.y+y)*p->root->width+(ScU32)area.x;
        memset(row,0,(ScU32)area.width*4u);
    }
    return p->error;
}

/* 文字颜色仍是现有主题命名角色；临时更改只活在本次调用，不能污染
 * 后续控件或借用不透明Painter设置裸RGB。旧Text入口语义原样保留。 */
ScStatus ScUiPainterTextColor(ScUiPainter *p,const ScUiRect *r,const ScUiText *text,
    ScU32 font_role,ScU32 flags,ScU32 color_role)
{
    if(!p || color_role>=24u)return SC_UI_E_INVALID;
    ScU32 previous=p->text_role;p->text_role=color_role;
    ScStatus result=ScUiPainterText(p,r,text,font_role,flags);
    p->text_role=previous;return result;
}

ScStatus ScUiPainterBounds(ScUiPainter *p,ScUiRect *out)
{
    if(!p || !out)return SC_UI_E_INVALID;
    ScUiRect value=p->clip;value.x-=p->origin_x;value.y-=p->origin_y;
    *out=value;return p->error;
}
