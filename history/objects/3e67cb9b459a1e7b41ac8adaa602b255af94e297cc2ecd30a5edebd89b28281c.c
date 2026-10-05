
#define INC(x) ((x)+1)
#define ALIAS INC
#define ID(x) x
#define TEXT(x) #x
#define CAT(x,y) x##y
#define WITH_EMPTY(x,y) prefix x##y
#define VAR(first,...) first + __VA_ARGS__
#define OBJECT (7)
#define LOOP LOOP
INC(INC(2))
ALIAS(3)
ID(INC)(4)
TEXT(a+b)
TEXT(a /* comment */ + b)
TEXT(0x10U)
CAT(he,llo)
WITH_EMPTY(,tail)
VAR(1,2,3)
OBJECT
LOOP
#if defined(INC) && !defined(NO) && (1 || 1/0)
chosen
#else
wrong
#endif
#undef OBJECT
#ifdef OBJECT
wrong2
#elif 4*3==12
elifchosen
#endif
#include "guard.inc"
#include "guard.inc"
