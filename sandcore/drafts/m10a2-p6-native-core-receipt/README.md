# 原生发行绑定 CORE：只读凭证

2026-10-16，已冻结。本目录只读取 Root 已生成的日志/产物并写小型凭证，没有构建、启动 VM、修改 main 或复制盘/大报告。Root 报告实际 make 退出0；审阅者没有观察构建进程或构建前后快照。凭证里的输入守恒仅指本次只读检查窗口。

- [core-build-receipt.json](core-build-receipt.json)：产物摘要、ELF/flat/SKM2/软盘一致性、实际编译的发行表与公钥、run03/HOST03来源。
- [core-build-inputs.json](core-build-inputs.json)：实际 `.d`、294个非生成编译依赖、汇编/对象、6个生成头、DHCP私有派生源与生成输入分别列出。
- [make_receipt.py](make_receipt.py)：只读检查实现；只在本目录生成上述两个 JSON，不调用 make、编译器、打包器写入口或 QEMU。

真实 CORE.SKM 为597712B，SHA256 `6f697fd6c09d38e409504ad987d3de945d47593f565678fde799ea4944989811`；载荷597584B、BSS384840B，入口RVA0。复用正式 inspect_elf/check_main，只调用检查函数；另外逐个比对 ELF 所有实际 ALLOC 正文与 flat、对齐空隙为零、CORE正文等于flat。loader.elf/loader.bin 同样逐节相等，sandcore.img 精确等于 boot＋loader＋零填充。

从真实 core.elf 符号地址读取 flat 中的 login_issued_sha256、shell_host_issued_sha256 和六条 login_issued_resources，逐项匹配 run03 实际 LOGIN2、SCSHELL、五库及固定 UAX。没有只信 issuer 头。程序长度同时核生成头与实际原生产物；这些标量已被编译器内联，不冒充独立 ELF 数据符号。公钥从真实 CORE读取，等于指定32B公开key，也等于 HOST03原报告所绑定的实际旧CORE公钥。

94份 `.d` 的294个非生成依赖当前 SHA 全等于 run03 preflight 指向的 HOST03 source pins；4个汇编源同样匹配，97个链接对象与日志/依赖目标闭合。3个原生 issuer 头与HOST bootstrap版本有意不同；公钥/两份字体生成头保持相同，DHCP派生源另列。HOST03原始 source pins 未包含 raw public key、core.ld、font16.txt、phoenix-ascii.json；凭证诚实标为当前显式生成输入，不能据此声称观察到它们的旧源码快照。公钥与字体生成内容另同现存HOST03产物比对。

Root 本次改动的 verify_m10a2_scl.py metadata 工具不在实际 `.d` 编译闭包，单列其旧/新 SHA，不将它说成已重新编译的内核源。

run03 原报告为完整 DECLARED_P6_NATIVE_GENERATION_PASS，8库/21artifacts/27SCO、owner physical_delta=0、原盘/输入未改。这里只读取并绑定该证据，**没有启动本 CORE、没有证明客体启动/最终GUI，也没有运行或验收SDK staging**。下一步由 Root 使用本CORE和真实原生产物完成统一客体流程；不用为本凭证重编未变组件或扩测试矩阵。
