#ifndef SANDCORE_SC32TERMINAL_H
#define SANDCORE_SC32TERMINAL_H
#include "SC32API.H"
/* PTY是本人能力，不是物理串口；master的普通ScFile复制/关闭保有真实引用。 */
enum {
    SC32_TERMINAL_MASTER=8,SC32_TERMINAL_SLAVE=9,
    SC32_TERMINAL_RAW=1,SC32_TERMINAL_APPLICATION_ECHO=2,
    SC32_TERMINAL_INTERRUPT_EVENTS=4,SC32_TERMINAL_HUNG_UP=8,
    SC32_TERMINAL_INTERRUPT=1,SC32_TERMINAL_EOF=2,SC32_TERMINAL_HANGUP=3,
    SC32_TERMINAL_MAX_COLUMNS=512,SC32_TERMINAL_MAX_ROWS=256
};
typedef struct {
    ScU32 size,version,columns,rows,flags,reserved[3];
}
ScTerminalDesc;
typedef struct {
    ScU32 size,version;
    ScJob job;
    ScFile master;
    ScU32 flags,reserved[3];
}
ScInteractiveInfo;
typedef struct {
    ScU32 size,version,kind,columns,rows,geometry_revision,input_bytes,output_bytes;
    ScU32 flags,interrupt_revision,slave_readers,slave_writers,reserved[4];
}
ScTerminalInfo;
/* master读合并且有序的stdout/stderr，写原始stdin；均遵守ScFile的AGAIN/EOF。
 * shell自己编辑/回显；不在PTY内再次做canonical编辑，不冒充POSIX termios。 */
ScStatus ScProcessSpawnInteractive(const char *,const ScTerminalDesc *,ScInteractiveInfo *out);
ScStatus ScTerminalQuery(ScFile master,ScTerminalInfo *out);
ScStatus ScTerminalResize(ScFile master,ScU32 columns,ScU32 rows);
ScStatus ScTerminalControl(ScFile master,ScU32 action);
#endif
