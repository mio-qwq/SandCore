/* 平表只有根的直属行。CHILD_AT的index是序号，ITEM/CELL的item是
 * 身份；混用会把item=0下溢并让正常重绘变成BUSY。此私有解析器
 * 统一三张表的边界，INDEX_OF按公开合同通过total返回序号。 */
static ScStatus flat_provider(const ScUiDataRequest *q,ScUiDataReply *r,ScU32 count,ScU32 revision,ScU32 *index)
{
    r->revision=revision;
    r->parent=0;
    r->total=count;
    if(q->operation==SC_UI_DATA_COUNT)return SC_UI_HANDLED;
    if(q->operation==SC_UI_DATA_CHILD_COUNT){
        if(q->parent>count)return SC_UI_E_NOT_FOUND;
        if(q->parent)r->total=0;
        return SC_UI_HANDLED;
    }
    if(q->operation==SC_UI_DATA_CHILD_AT){
        if(q->parent>count)return SC_UI_E_NOT_FOUND;
        if(q->parent || q->index>=count)return SC_UI_END;
        *index=q->index;
    }else if(q->operation==SC_UI_DATA_ITEM || q->operation==SC_UI_DATA_CELL || q->operation==SC_UI_DATA_INDEX_OF){
        if(!q->item || q->item>count)return SC_UI_E_NOT_FOUND;
        *index=q->item-1;
    }else return SC_UI_E_NOT_SUPPORTED;
    r->item=*index+1;
    r->state=SC_UI_VISIBLE|SC_UI_ENABLED;
    if(q->operation==SC_UI_DATA_INDEX_OF)r->total=*index;
    return q->operation==SC_UI_DATA_CELL?0:SC_UI_HANDLED;
}

