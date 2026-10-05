#ifndef SCNANO_H
#define SCNANO_H
#include "SCIO.H"
/* 自写nano只借用调用者已有的TTY/SERIAL能力；编辑内容在私有三环
 * 内存，保存才提交COW文件。不是GNU nano源码或标准C运行库移植。 */
static int nano_main(void);
#endif
