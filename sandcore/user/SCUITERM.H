#ifndef SANDCORE_SCUITERM_H
#define SANDCORE_SCUITERM_H
#include "SCUI.H"
/* 正文只存在于SCTERM.SCL；此接口也可承载串口/网络终端模拟，不拥有传输。 */
typedef struct ScUiTerminal ScUiTerminal;
enum {
    SC_UI_TERM_CURSOR=1,SC_UI_TERM_WRAP=2,SC_UI_TERM_ALTERNATE=4,
    SC_UI_TERM_BRACKETED_PASTE=8,SC_UI_TERM_APPLICATION_CURSOR=16,
    SC_UI_TERM_BOLD=1,SC_UI_TERM_UNDERLINE=2,SC_UI_TERM_INVERSE=4,
    SC_UI_TERM_WIDE=8,SC_UI_TERM_CONTINUATION=16,
    SC_UI_TERM_CLEAR_SCREEN=1,SC_UI_TERM_CLEAR_HISTORY=2,SC_UI_TERM_RESET=3
};
typedef struct {
    ScU32 size,version,parent,columns,rows,history_rows,input_budget_bytes,font_role;
    ScU32 flags,reserved[3];
}
ScUiTerminalDesc;
typedef struct {
    ScU32 size,version,node,columns,rows,cursor_column,cursor_row,flags;
    ScU32 history_rows,history_limit,view_back,input_bytes,revision,bells,invalid_utf8;
    ScU32 first_row,last_row,font_role,reserved;
}
ScUiTerminalInfo;
typedef struct {
    ScU32 size,version,anchor_row,anchor_column,active_row,active_column,reserved[2];
}
ScUiTerminalSelection;
typedef struct {
    ScU32 size,version,scalar,foreground,background,flags,row_id,reserved;
}
ScUiTerminalCell;
ScStatus ScUiTerminalCreate(ScUiContext *,const ScUiTerminalDesc *,ScUiTerminal **out,ScUiNode *node);
ScStatus ScUiTerminalRetain(ScUiTerminal *);
void ScUiTerminalRelease(ScUiTerminal *);
ScStatus ScUiTerminalQuery(const ScUiTerminal *,ScUiTerminalInfo *out);
/* 错误也返回已接受前缀；调用者保留剩余字节，不能重复提交已接受的部分。 */
ScStatus ScUiTerminalFeed(ScUiTerminal *,const void *,ScU32 bytes,ScU32 *accepted);
ScStatus ScUiTerminalFinish(ScUiTerminal *);
ScStatus ScUiTerminalResize(ScUiTerminal *,ScU32 columns,ScU32 rows);
ScStatus ScUiTerminalClear(ScUiTerminal *,ScU32 action);
ScStatus ScUiTerminalSetFont(ScUiTerminal *,ScU32 font_role);
ScStatus ScUiTerminalScroll(ScUiTerminal *,ScI32 rows);
ScStatus ScUiTerminalSelect(ScUiTerminal *,const ScUiTerminalSelection *);
ScStatus ScUiTerminalCopySelection(const ScUiTerminal *,ScString **out);
ScStatus ScUiTerminalCopyTranscript(const ScUiTerminal *,ScString **out);
ScStatus ScUiTerminalCopy(ScUiTerminal *);
ScStatus ScUiTerminalPaste(ScUiTerminal *);
ScStatus ScUiTerminalSend(ScUiTerminal *,const void *,ScU32 bytes,ScU32 paste);
ScStatus ScUiTerminalTakeInput(ScUiTerminal *,void *,ScU32 capacity,ScU32 *taken);
ScStatus ScUiTerminalCellQuery(const ScUiTerminal *,ScU32 row,ScU32 column,ScUiTerminalCell *out);
ScStatus ScUiTerminalTitle(const ScUiTerminal *,ScString **out);
#endif
