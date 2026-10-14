# M10a2 SCCC 原生 SCL 检查点

SandCore M10a2 — The Application Platform Era，2026-10-14。

`guest.json` 是 Windows 双盘 **DECLARED_NATIVE_SCL_AND_G1_CASES_PASS**：HOST_BOOTSTRAP编译器SCCC0在客体生成两库，随后在客体生成编译器G1，G1真实执行并再生成相同库字节，两代均真实装载调用。它不是GCC/SCCC编译器映像相同或完整自举固定点结论。

| 文件 | 来源与范围 |
|---|---|
| build.json | native-build-02，只构建SCCC0，实际大小/源摘要/退出码 |
| guest.json | native-guest-02，原生编译、G1执行、两库调用、代表旧格式、失败保留、退出页回收 |
| BASE.SCL / UPPER.SCL | 由客体实际生成的G1编译器输出，732/1660B |
| SCCC-G1.SCX | 客体内SCCC0生成并已实际执行的G1，190489B；不是宿主复制 |
| host-compiler.json | 宿主I/O适配下的真实前后端证据，main被替换的覆盖边界明确 |
| rootless-host.json | 保留真实CLI/GUI main的四项ROOTLESS/63B/日志别名必要回归 |
| first-path-failure.json | native-guest-01重复RESOLVE的真实失败，未改称通过 |
| cleanup.json | 仅失败批两张生成盘清理清单，错误代码已提取保留 |
| desktop.png | 实际HMP输入后1920×1080桌面，原界面未重制 |

SCCC0的load108337B/BSS3535343B；G1的load190453B/BSS3535004B。G1完整SHA256为`3830fca861b3c3a7dc3852f981b65d1ecd5fcdd2d3778f393ca1e60afc18432e`。BASE/UPPER均与SCCC0在客体生成的对应库字节相同。

同一VM完成相关错误/旧模式检查，来源盘与当前输入SHA不变。执行上下文是外部SYSTEM Shell的Ring3孩子，尚未声称完整普通身份/GUI/多进程及动态模块调试验收。动态SCX启动依赖、四大正式库/SCUI/桌面与应用重制/SDK/性能仍待，见 [恢复入口](../../docs/M10A2-IMPLEMENTATION.md)。

本机保留失败日志/截图及`build/m10a2-native-guest-01/failed-code`，当前工作盘为`build/m10a2-native-guest-02/sanddata.img`；没有为每个步骤再复制数据盘。

修订：2026-10-14，保存真实SCCC0/G1原生两库输出与调用、首轮失败和必要修复证据，保持完整目标边界。
