#ifndef SANDCORE_LWIP_PORT_H
#define SANDCORE_LWIP_PORT_H
#include <stddef.h>
/* 私有适配层只向lwIP翻译单元发布；不能将这些名字放入SCAPI或
 * /SYS/INC当作通用libc。全部内存由真实PF页持有并有独立回收统计。 */
void *sc_net_alloc(size_t bytes);
void *sc_net_calloc(size_t count,size_t bytes);
void sc_net_free(void *pointer);
/* 私有故障现场，只记录坏释放的分配布局和调用地址，不读取载荷。 */
extern unsigned int sc_net_free_fault[12];
void *sc_net_memcpy(void *to,const void *from,size_t bytes);
void *sc_net_memmove(void *to,const void *from,size_t bytes);
void *sc_net_memset(void *to,int value,size_t bytes);
int sc_net_memcmp(const void *a,const void *b,size_t bytes);
size_t sc_net_strlen(const char *value);
int sc_net_strcmp(const char *a,const char *b);
int sc_net_strncmp(const char *a,const char *b,size_t bytes);
char *sc_net_strchr(const char *value,int byte);
char *sc_net_strstr(const char *value,const char *find);
int sc_net_atoi(const char *value);
unsigned int sc_net_rand(void);
void sc_net_assert(const char *why);
void sc_net_core_assert(void);
unsigned int sc_net_pages(void);
unsigned int sc_net_peak_pages(void);
unsigned int sc_net_used(void);
/* 单CPU，所有raw API操作在IF=0或task_render_hold期间串行；IRQ
 * 永远不进lwIP。进入/退出仅保护协议状态，不把中断关闭当作调度预算。 */
void sc_net_context(int entered);
int sc_net_random_init(void);
unsigned int sc_net_isn(unsigned int local,unsigned int remote,unsigned short local_port,unsigned short remote_port);
int sc_net_timer_failed(void);
int sc_net_timers_prepare(void);
#endif
