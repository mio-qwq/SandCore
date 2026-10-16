#ifndef SANDCORE_SYSTEM_TOOLS_PRIVATE_H
#define SANDCORE_SYSTEM_TOOLS_PRIVATE_H
#include <SC32API.H>
#include <SC32PATH.H>
#include <SCUI.H>
#include <SCSHELL.H>
#include <SCLIB.H>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

typedef struct ToolWindow ToolWindow;
typedef ScStatus (*ToolCommandProc)(ScU32);
typedef ScStatus (*ToolEventProc)(ScUiContext *,const ScUiEvent *,void *);
struct ToolWindow {
    ScUiContext *ui;
    ScUiNode root,menu,toolbar,content,status;
    ScUiMenu *menus;
    ScUiDialog *notice;
    ScUiMetrics metrics;
    ScU32 visible,quit;
    ToolCommandProc command;
    ToolEventProc event;
    char status_text[320];
};
typedef struct {const char *label;
    ScU32 command,key,modifiers;
} ToolMenuEntry;
enum {TOOLS_EXIT=0x6000,TOOLS_ABOUT,TOOLS_REFRESH,TOOLS_COPY,TOOLS_COPY_DETAIL};
static ToolWindow tool;
static ScUiText tool_text(const char *z);
static void tool_status(const char *z);
static ScStatus tool_error(ScStatus s,const char *operation);
static ScStatus tool_layout(ScUiNode node,ScU32 mode,ScI32 width,ScI32 height,ScU32 flex);
static ScStatus tool_widget(ScUiNode parent,ScU32 kind,const char *text,ScU32 command,ScI32 width,ScI32 height,ScU32 flex,ScUiNode *out);
static ScStatus tool_listen(ScUiNode node,ScU32 phases,ToolEventProc proc,void *context);
static ScStatus tool_menu_part(const char *title,const ToolMenuEntry *items,ScU32 count);
static ScStatus tool_menu_item(ScUiMenu *menu,const ToolMenuEntry *entry,ScU32 flags);
static ScStatus tool_copy(const char *text);
static ScStatus tool_message(const char *title,const char *message);
static ScStatus tool_string_node(ScUiNode node,ScString **out,const char **z);
static ScStatus tool_reserve(void **buffer,ScU32 *capacity,ScU32 count,ScU32 bytes);
static void tool_visible(ScUiNode node,ScU32 visible);
static ScU32 tool_percent(ScU32 value,ScU32 total);
static ScStatus tool_create(const char *title,ScU32 width,ScU32 height,ToolCommandProc command,ToolEventProc event);
static void tool_close(void);
#endif
