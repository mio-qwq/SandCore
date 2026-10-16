# 稳定读共享打开修复稿

Sound 的当前对象与候选对象都通过 ScFileLeaseOpenPinned 申请 READ_STABLE。第二个请求被 `fs_lease_open.inc` 的 CONTENT 冲突挡住；直接改 READ_OPEN 会同时放过写租约，不能保持稳定性。

本组只改三个内核源文件的局部 hunk，并给 SC32-FILE.md 追加合同说明：增加内部 STABLE_OPEN，仅稳定读 Acquire/OpenPinned 使用；仅健康空闲的同名稳定读可共存，仍阻止写租约、slot1 目标预留和正在改名/删除/改权限的租约。详细实际边界见 [CONTRACT.md](CONTRACT.md)。

旧权限入口本来允许合法 owner/管理者改权限，后续 IO 重新验权；本次没有把旧调用行为悄改成 BUSY。typed lease SetMode 的 unique/TREE 冲突保持。原公开头、SCAPI 字节、请求布局、selector、磁盘记录均不改。

`stable-read-sharing.patch` 与 `PINS.json` 固定三个 C 前像和文档前像；`overlay/` 只供审阅，应用精确 hunk而非整文件覆盖。`SOURCE-PINS.json` 固定实际审阅的权限、native writer、绑定/别名、operation 和 Sound 调用来源。生成器只做唯一锚点/差异/源码未变断言，未调用编译器，没有客体证据。

状态为 `SOURCE_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED`。主树、gen04、旧草稿、镜像未改；Root 仍可选择 worker 原位 seek，本稿不预先合入或改 Sound 路线。
