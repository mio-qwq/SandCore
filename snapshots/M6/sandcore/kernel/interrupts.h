#ifndef SANDCORE_INTERRUPTS_H
#define SANDCORE_INTERRUPTS_H

/* 中断子系统对外的全部接口 (实现见 interrupts.c) */

void interrupts_init(void);              /* IDT + PIC + 开中断, 一次到位 */
void panic(const char *reason, u32 vec); /* 红屏报异常, 永久停机 */

/* C 侧分发函数, 由 idt.asm 的公共桩调用 —— 不在头文件承诺原型细节 */
/* void isr_c_common(u32 vec);  void irq_c_common(u32 vec); */

#endif /* SANDCORE_INTERRUPTS_H */
