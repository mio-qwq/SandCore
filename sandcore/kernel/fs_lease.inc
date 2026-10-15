/* 所有片段是自写freestanding后端，包含在fs.c末尾以复用真实v5
 * 静态目录/位图/CRC/原权限。正式接线前仅是隔离源码，不是运行PASS。 */
#include "fs_lease_index.inc"
#include "fs_lease_native.inc"
#include "fs_lease_open.inc"
#include "fs_lease_operation.inc"
#include "fs_lease_commit.inc"
#include "fs_lease_cleanup.inc"
