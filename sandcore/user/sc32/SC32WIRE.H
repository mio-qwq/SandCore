#ifndef SANDCORE_SC32WIRE_H
#define SANDCORE_SC32WIRE_H

/* 仅系统库与内核协调层共享的私有字布局。普通应用不使用gate/selector。
 * 公共类型没有软中断桥，所有多字记录遵循32位小端、4字节对齐。 */
#include "../SC32API.H"
#define SC32_WIRE_GATE 0x300u
#define SC32_WIRE_VERSION 1u
#define SC32_WIRE_MAX_RECORD 65536u
#define SC32_WIRE_PROBE 0x01u
#define SC32_WIRE_CAPABILITIES 0x02u
#define SC32_WIRE_PROCESS 0x03u
#define SC32_WIRE_MEMORY 0x04u
#define SC32_WIRE_MONOTONIC 0x05u
#define SC32_WIRE_READ 1u
#define SC32_WIRE_WRITE 2u
typedef struct {
    ScU32 size,version,address,bytes,access;
} Sc32ProbeRequest;

/* 01输入严格20B，out=NULL；bytes=0无访问，允许NULL但access仍须合法。
 * 02/04/05输入=NULL，输出为64/64/32B公共v1记录。
 * 03输入严格16B ScProcessRef；pid=gen=0取self，否则活对象+精确代数。
 * 所有输出先读8B前缀，检查version=1、size在规范尺寸..65536；再检查
 * 完整声明范围RW。成功仅写规范字节，size报告规范尺寸，未知尾部保持。
 * 错误不改输出。坏映射/只读/尺寸=-101，版本=-102，陈旧=-108。
 * capability位只有真正存在的服务置1；消息/更新文件/剪贴板/IPC/共享映射
 * 未接通时为0，不能从旧FRAME或oldstream推断它们已实现。 */
#define SC32_WIRE_GUI_FIRST 0x100u
#define SC32_WIRE_FILE_FIRST 0x200u
#define SC32_WIRE_CLIPBOARD_FIRST 0x300u
#define SC32_WIRE_IPC_FIRST 0x400u
#define SC32_WIRE_SHARED_FIRST 0x500u

#endif
