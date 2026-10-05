# M9 SSE2 整数与扩展状态

> 当前验证范围已更新，见末尾“2026-10-05 M9最终验证记录”及[M9验收报告](M9-ACCEPTANCE.md)。早期未验证/待验标记保留阶段背景。

> 已整包构建并实际执行整数SSE2/XMM7/MXCSR/x87分项；无SSE2与FNSAVE
> 双盘8组回退分项通过，完整异常和性能仍在验证，不能称全面验收。

早期检测EFLAGS.ID/CPUID、FPU、FXSR、SSE、SSE2。可用时CR0清EM/TS并设MP/NE，
CR4设OSFXSR/OSXMMEXCPT；FXSAVE区域512B、16B对齐，每任务独立保存x87/MMX、
XMM0..7与MXCSR。初始x87 FNINIT、MXCSR 0x1f80、寄存器正文清零，任务创建/
退出/槽复用重置。只有FPU而无FXSR时FNSAVE/FRSTOR 108B回退；内核C仍禁浮点、
SSE、MMX，只在明确的状态切换路径使用管理指令。

调度只在实际任务切换保存/恢复，不能每次PIT无条件搬512B。新0x21D SIMDINFO
32B为version,ready,fxsr,sse2,CPUID_EDX,state_bytes,switches,0。调用者先判断
SSE2，再执行向量路径；SCX flags不借位，既有bit1仍属于图标容器扩展。

SCCC内联汇编区分xmm与GPR，支持movdqa/movdqu、pshufd、整数加减/饱和、
逻辑/比较、乘法、pack/unpack、min/max/average/SAD。__SCCC_SSE2__只表示
编译器编码能力，不等于当前CPU支持。SCSIMD.H用指针函数给出add4/xor4
及纯整数回退，不要求C语言首先具备128位类型。SandAsm使用Intel顺序，
另支持movd、pmovmskb、整数移位的立即数/向量操作数，内存支持[reg+disp32]
和[absolute]。movdqa的16B对齐责任不被悄悄抹掉。

第二阶段须用两个含不同XMM/MXCSR/x87模式的被抢占任务、异常、退出/槽复用、
无SSE2 CPU及历史SCX，核对状态与旧缓冲边界；在相同数据规模实测收益后才可
称SIMD优化达标。当前MP3标量解码没有SIMD收益声明。

修订：2026-10-04，新增扩展状态、源API及两个编译器的整数SSE2合同，未验证。

2026-10-05实际补充：06/18执行原生编译的整数SSE2以及独立状态夹具，
初始隔离、两个不同模式任务的让出/抢占和槽复用通过。固定TCG CPU配置
另外检测qemu32禁SSE2、禁FXSR/SSE/SSE2以及禁FPU；以实际SIMDINFO快照
核对ready/fxsr/sse2/state_bytes，不只相信命令行。FPUSTATE先读默认
控制字/TOP/tag，再写各自控制字与整数栈值；整数忙段覆盖PIT IRQ，另有
yield，退出不先清现场掩盖槽复用错误。宿主编译仅生成测试夹具，内核
仍无浮点/libc；无CPU能力时先返回unsupported，不执行被禁的指令。
当前fallbacks-01双盘无SSE2/FXSAVE、FNSAVE、无FPU及缺音频设备共8组
68项通过；不是所有CPU架构、异常或收益的完整证明。

修订：2026-10-05，登记实际整数与扩展现场证据，追加固定能力/旧式保存/无FPU回退矩阵，性能收益未宣称达标。

## 2026-10-05 M9最终验证记录

每盘245种SSE2整数汇编形式与GNU as全部机器码一致，六类非法输入保留旧输出；真实并发XMM7/MXCSR/x87抢占/复用、FXSAVE/FNSAVE/无SSE2/无FPU回退与最后内核重测通过。编码对照不等同全部生成流已执行或全组件加速收益。

修订：2026-10-05，记录实际范围与证据，待用户验收；前述早期未验证叙述保留为历史。
