#ifndef SANDCORE_SC32DROPWIRE_H
#define SANDCORE_SC32DROPWIRE_H
#include "../sc32/SC32BROKERWIRE.H"
#include "../SC32DROP.H"
enum {
    SC32_D_REGISTER=0x420u,SC32_D_UNREGISTER=0x421u,SC32_D_AT=0x422u,
    SC32_D_VALIDATE=0x423u,SC32_D_CONNECT=0x424u,SC32_D_COMPLETE=0x425u,
    SC32_D_CANCEL=0x426u,SC32_D_OPERATION=0xe00u
};
typedef struct {
    ScU32 size,version,window,generation,endpoint,actions,formats,format_count,flags,reserved[3];
} Sc32DropRegister;
typedef struct {ScU32 size,version,window,generation;} Sc32DropWindow;
typedef struct {ScU32 size,version,ticket_lo,ticket_hi,action,reserved;} Sc32DropComplete;
/* AT32、VALIDATE32、TOKEN16直接采用无指针公共记录；Register48把
 * formats指针转本人地址值。注册完整拷贝format表，内核不持用户VA。
 * source/target ticket核实全由Root的真实WM/Broker服务完成，尚未接线。 */
#endif
