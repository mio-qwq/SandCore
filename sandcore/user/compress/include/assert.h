#pragma once
/* 内部不变量失败退出当前三环任务，绝不能继续发布受损的压缩结果。 */
#define assert(c) do {if(!(c))sc_exit(125);} while(0)
