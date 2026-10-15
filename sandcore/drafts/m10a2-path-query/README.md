# 现代路径元数据增量

2026-10-16，仅独立实现草稿，未接入、未构建、未运行。

实际缺陷是worker目标探测期待NOT_FOUND，而旧PathMetadata把旧-1映射为粗FAILED。不能把所有FAILED改译为不存在，也不改变已发布旧入口的错误语义。新增SC32PATH.H / ScPathQuery，返回既有48B ScPathInfo和明确现代错误；不打开正文、不分配文件句柄、不要求文件正文R。不可遍历祖先优先ACCESS，挂载/IO不明不返回假缺名，失败不改out。

拟占SC32文件族新selector0x223、32B私有请求，所有保留位为0。SC32ABI1.3保留，实际库版本升5，新消费者要求v5；旧导出、记录、selector保留。独立公共头避免把新声明混入旧SCAPI，SC32API.H仅同步实际library version常量。复用旧调用方时必须记录ABI兼容依据，不能把旧caller编译冒充本轮。

接线需要fs.h新增内核原型、fs/sc32_file包含新片段、SC32聚合/manifest新增真实导出和能力位，构建/SDK完整接线；迁移SCSHELL/worker中需要精确缺名的元数据查询。随后在同一代表文件job里验证缺名、已存在、真实权限以及COPY/MOVE最终字节/ACK，不扩理论矩阵。

当前名字仍沿既有SandFS 63B且不含ASCII空格的规则。现代路径空格需要另外的版本化规范化/目录与旧文本枚举兼容合同，本草稿不能冒称已经完成该项。
