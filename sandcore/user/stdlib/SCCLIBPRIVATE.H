#ifndef SCCLIB_PRIVATE_H
#define SCCLIB_PRIVATE_H
#include "stdlib.h"
#include "string.h"
#include "stdio.h"
#include "ctype.h"
#include "errno.h"
#include "limits.h"
#include "stdint.h"
#include "SC32API.H"
/* R1由根运行时实现；旧九导出ROOT不支持，manifest最低版本明确拒绝。 */
int ScApplicationRegisterPrepare(void (*prepare)(void));
int ScApplicationPinCallback(void (*callback)(void));
void ScApplicationExit(int status);
int SccLibraryInit(void);
void SccLibraryFini(void);
static void __scc_heap_finish(void);
static int __scc_stdio_init(void);
static void __scc_stdio_finish(void);
static void __scc_prepare(void);
static void __scc_callbacks_finish(void);
static void __scc_callbacks_init(void);
static void __scc_environment_finish(void);
static int __scc_from_status(ScStatus status);
static int __scc_stdio_failed(FILE *stream);
static void __scc_early_write(const char *text);
#endif

