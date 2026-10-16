#ifndef SANDCORE_SCTERMPRIVATE_H
#define SANDCORE_SCTERMPRIVATE_H
#include "SCUITERM.H"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
typedef struct {
    ScU32 scalar,foreground,background,flags;
    ScString *marks;
}
TermCell;
typedef struct {
    ScU32 id,columns,used,wrapped;
    TermCell *cells;
}
TermRow;
typedef struct {
    TermRow **lines;
    ScU32 columns,rows,column,row,saved_column,saved_row,top,bottom,pending,origin,insert;
}
TermScreen;
typedef struct TermClass TermClass;
struct ScUiTerminal {
    ScU32 magic,refs,node,node_owned,revision,flags,font_role,cell_width,cell_height,font_epoch;
    ScUiContext *context;
    TermClass *owner;
    ScUiTerminal *previous,*next;
    TermScreen primary,alternate,*screen;
    TermRow **history;
    ScU32 history_limit,history_count,history_head,view_back,next_row;
    unsigned char *input;
    ScU32 input_capacity,input_head,input_used,input_budget;
    ScU32 foreground,background,attributes,parser,parameters[16],parameter_count,private_mode,intermediate;
    ScU32 utf8_scalar,utf8_needed,utf8_seen,utf8_min,bells,invalid_utf8;
    char osc[512];
    ScU32 osc_used,osc_overflow;
    ScString *title;
    ScUiTerminalSelection selection;
    ScU32 selecting,scroll_grab;
    ScI32 width,height,dirty_first,dirty_last;
    ScU32 has_damage;
    struct {
        ScU32 scalar,width,epoch;
    }
    widths[256];
};
struct TermClass {
    ScUiContext *context;
    ScUiClass kind;
    ScUiTerminal *first;
    TermClass *next;
};
static TermClass *term_classes;
enum {
    TERM_MAGIC=0x5343544d,TERM_GROUND=0,TERM_ESC=1,TERM_CSI=2,TERM_OSC=3,TERM_OSC_ESC=4,TERM_CHARSET=5
};
static void t_record(void *p,ScU32 bytes){
    memset(p,0,bytes);
    ((ScU32 *)p)[0]=bytes;
    ((ScU32 *)p)[1]=1;
}
static int t_valid(const ScUiTerminal *m){
    return m && m->magic==TERM_MAGIC && m->refs;
}
static ScStatus t_output(void *out,ScU32 bytes)
{
    return !out?SC_UI_E_INVALID:((ScU32 *)out)[1]!=1?SC_UI_E_VERSION:((ScU32 *)out)[0]<bytes?SC_UI_E_INVALID:0;
}
static void t_damage(ScUiTerminal *,ScI32,ScI32);
static void t_flush_damage(ScUiTerminal *);
static ScStatus t_metrics(ScUiTerminal *);
static ScStatus t_byte(ScUiTerminal *,unsigned char);
static ScStatus t_queue(ScUiTerminal *,const void *,ScU32);
static void t_node_drop(void *,void *);
#endif
