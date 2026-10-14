#ifndef SCCLIB_STDBOOL_H
#define SCCLIB_STDBOOL_H
#if defined(__SCCC__) && !defined(__SCCC_BOOL__)
#error This SDK needs the real SCCC boolean type
#endif
#define bool _Bool
#define true 1
#define false 0
#define __bool_true_false_are_defined 1
#endif

