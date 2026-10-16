#ifndef SANDCORE_CALC2_PRIVATE_H
#define SANDCORE_CALC2_PRIVATE_H
#include "TOOLS.H"
enum {CALC_E_SYNTAX=-601,CALC_E_OVERFLOW=-602,CALC_E_DIVZERO=-603,CALC_E_DEPTH=-604,CALC_E_EMPTY=-605,
    CALC_EVALUATE=0x6200,CALC_CLEAR,CALC_BACKSPACE,CALC_COPY_HEX,CALC_UNDO,CALC_REDO,CALC_RECALL,
    CALC_ANS,CALC_NEGATE,CALC_KEY_FIRST=0x6300};
typedef struct {ScStatus status;
    ScU32 offset,bits;
} CalcResult;
typedef struct {ScU32 bits,wide;
} CalcValue;
typedef struct {const char *text;
    ScU32 bytes,at,depth;
    CalcResult result;
} CalcParser;
typedef struct {ScString *expression;
    char decimal[24],hex[24];
    ScU32 bits;
} CalcHistory;
typedef struct {
    ScUiNode input,answer,hex,table,keyboard,result_row,history_splitter;
    ScUiDocument *document;
    ScArray *history;
    ScUiProvider *provider;
    ScUiSelection *selection;
    ScU32 revision,valid,last_valid,bits,layout_stamp;
    char decimal[24],hexadecimal[24];
} CalcApp;
static CalcApp calc;
#endif
