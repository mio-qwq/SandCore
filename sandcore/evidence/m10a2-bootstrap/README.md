# M10a2 SCL 引导检查点

SandCore M10a2 — The Application Platform Era，2026-10-14。

此目录仅保存一个引导切片的绑定输入与结果，避免提交重复数据盘。
完整未完成范围和恢复顺序见 [M10A2-IMPLEMENTATION](../../docs/M10A2-IMPLEMENTATION.md)。

| 文件 | 实际含义 |
|---|---|
| build.json | WSL ELF 定向构建，真实退出0，输入及产物 SHA256 |
| host-runtime.json | 真 mmap/mprotect/UBSan 的装载算法/回滚/寿命验证，init/fini 模拟，未执行i386 |
| format-tests.log | C/Python格式与拒绝码、独立重定位字节及工具诊断的定向结果 |
| guest.json | 一次Windows软盘+IDE QEMU真实两库调用、权限、显式及退出回收、旧SCX ls |
| desktop.png | HMP实际输入后1920×1080桌面，已查看；当前桌面尚未重制 |

库和探针均是 HOST_BOOTSTRAP，`guest_sccc_compiled=false`。无共享代码页、NX 或完整 W^X 结论。两个 runtime 的地址/数据隔离是在同一进程的独立实例上观察，不能冒充同时存活的多个进程。首轮全局页账6838→6844的六页暖态差额保留；后两种退出精确回暖基点。此检查点不是整个 Phase 1 或 M10a2 的验收包。

本机详细日志/会话盘位于 `build/m10a2-bootstrap`、`build/m10a2-runtime-host`、`build/m10a2-scl-fixtures` 和 `build/m10a2-scl-guest-01`。构建、测试和对应源码均在仓库中，复现命令见 BUILD/SCL/SCLDR 规范；既有目录拒绝覆盖。

修订：2026-10-14，保存已实际执行的首个引导切片证据，保留宿主/客体及完整目标边界。
