# P6 SCLD 首次执行阻断：只读定位

2026-10-16。只读 run-02 实际报告、生成 SCLD、盘内库与同批源码；未构建、启动 QEMU、修改 main 或复制盘/大报告。Root 已自行修复并开始 HOST03 增量构建；这里仅确认源码因果与修法安全，不能称修后客体通过。输入、原构建源码和修后源码 SHA 见 INPUTS.json。

**不是缺失导入。** run-02 的 SCLD0.SCX 为73228B，SHA256 `570167927a4828760d637a98676cd728e9ada9cd6fa1d0018daa2f4f9c3ac4f9`。真实导入22项：SC32API的 ScPathResolve、ScProcessArguments，以及 SCCLIB 的20项。逐项实际 kind/最小宽度均满足，三库传递导入与版本亦全部满足。

| run-02 真实盘内文件 | 字节 / 实际 ABI / 导出 |
|---|---|
| /SYS/LIB/SCLDR.SCL | 20529 / 1.2 v3 / 12 |
| /SYS/LIB/SC32API.SCL | 154939 / 1.4 v6 / 193 |
| /SYS/LIB/SCCLIB.SCL | 39256 / 1.0 v1 / 94 |

SCLD实际要求 SC32API 1.3/v5 与 SCCLIB1.0/v1；当前 NATIVE manifest 的更高最低版本不是本次失败原因。无需删除、补造或放宽导入清单。

**确证错误链：** 原 `kernel/sc32_debug_modules.inc:141` 的 d2_module_begin 调用 `fs_native_open(pid,0,canonical,FS_NATIVE_READ,0,&source)`；同批 `kernel/fs.c:982` 检查 `!writer`，立即返回 FS_N_INVALID=-1。d2_module_begin 原第142行将它映射为 SC32_E_IO=-111；`user/runtime/scl_debug_registry.c:37` 再映射成 SC_SCL_IO=-11，返回 `scldr.c:237` 的登记钩子，最后由 `scl_application.c:71` 打印总括文字 `dependency or symbol (-11)`。真正缺符号为 SC_SCL_SYMBOL=-3，版本不足为-4（SCLDR.H:14-22）。bootstrap ROOT 由可信内核提前登记，首次普通 SC32API 装载登记才触发这个必败路径，因此 SCLD 的 main 尚未执行。

原 debug/FS/ROOT 源 SHA 已逐项对齐 run-02 引导的 `p6-unified-host-02.json`；该报告 SHA `55472b7cd477cf545a66ca1d106daff27c15ac53e336a262dfe9f2b7ebc5ddd6` 与 preflight 一致，不是用新源码猜旧二进制。

**Root 的最小修复安全。** writer 改为 `task_generation(pid)`，close 保持 `fs_native_close(source,0)`。调用固定 object初值0、READ、无CREATE/TRUNCATE/APPEND；readonly既有文件的成功路径只取得引用，不进入 native_pending_start，也不保存、匹配 writer。AGAIN/BUSY 分支在取得引用之前返回。后续 read/info 权限按真实 pid、对象和 fs_access；lease_native_guard 仅对WRITE核 writer，READ_OPEN 冲突只按路径/flags。任务代数非零、核验期间稳定，即便与另一个 h->id 数值相撞，也不会续进或取得其写权限。close传0使 fs_native_cancel 不匹配任何真实非零写事务，因此不会取消他人的 pending。

本次无需追加 AGAIN/BUSY→PENDING：它不是当前故障原因，只读失败没有候选资源；改变会新增可能无界的重试等待语义。保持 FS 合同、权限/lease检查、ROOT注册核验和 SCLD manifest 不变，只修内部非零调用身份。后续由 Root 对受影响 CORE 做增量构建并复用已成功的客体编译成果，实际执行出口由统一流程留证，本审阅到此冻结。
