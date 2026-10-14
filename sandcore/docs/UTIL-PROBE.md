# SCLIB 定向动态应用 UTILPROBE

所属版本：**SandCore M10a2 — The Application Platform Era**。本应用检查 [SCLIB.md](SCLIB.md) 的完整功能范围中七组相互关联的工作流。它是正常 `int main(void)` 动态应用，仅包含正式 `SCLIB.H`、`SC32API.H` 和小写标准头，所有库函数都由运行时解析；没有库正文、SCAPI、内联系统桥或显式 loader。

**已统一构建、系统内SCCC生成并在Windows双盘QEMU实际执行。** [真实证据](../evidence/m10a2-stdlib/README.md)包含HOST与native库各253检查、精确22B文件和退出页回收；真实输出/产物来源分列。预期协议与实际日志对应，仍不代表完整M10a2或大规模GUI性能达标。

## 1. 文件与运行条件

输入为 `tests/m10a2/UTILPROBE.C` 和 `UTILPROBE.json`。清单严格只有 `kind/name/dependencies/imports` 四键，name 为 `UTILPROBE.SCX`。直接依赖 SCLIB ABI1.0/version1、SCCLIB ABI1.0/version1、SC32API ABI1.1/version2；ROOT R1 由正式依赖链约束。124项实际导入分别属于 SCLIB104、SCCLIB17、SC32API3；标准 `errno/stdout` 的宏展开实体也纳入清单，未使用的公开原型不制造导入。

本次静态 include 图为11个唯一文件、76599原始字节，含每源终止字节76610B；265个不同定义宏加11个当前预定义共276。FILE32、SOURCE262144、MACRO384未超；这是当前公开头快照的容量核对，不是实际预处理、token/机器码/栈 ABI 或运行验证。

主代理将这两份源输入放入客体可写的 `/HOME/M10A2UTIL/` 测试目录，正式库放 `/SYS/LIB/`，公共 SDK 头放 `/SYS/INC/`。从该 HOME 目录运行：

```text
sccc UTILPROBE.C UTILPROBE.SCX --dynamic UTILPROBE.json
UTILPROBE.SCX
```

SDK、库和编译器必须为本轮实际输入，旧 Foundation 产物不能代替新增 SCLIB。应用不安装/修改系统库，文件工作流只覆盖当前 HOME 目录下专用 `UTILDATA.BIN`；运行前选定这个测试目录，不把它指向用户文件。应用读取一次真实 WorkingDirectory、检查 HOME 边界，再构造绝对文件名；SCLIB 的词法 Path 工具不代替内核文件 Resolve 或权限检查。

## 2. 七组工作流及实际判据

| 工作流 | 实际调用与观察 | 必须保持的协议 |
|---|---|---|
| Array/List/Hash | array 从原 View 别名插入并读取结果；list 在 visit 内删除当前/未来项、插入新项；hash 拥有含 NUL 的二进制 key、替换值、回调中拒绝替换正在借用的值 | 成功修改使旧代数失效；修改后 Next 返回 STALE 且 out 不动；新项排除入口水位，最外层返回才 drop；三个容器正常收束 drop 次数均4/4/3 |
| String/UTF8/Path/Config | 非 NUL 结尾的借用字节 COW、别名 append、取得真实 CStr；四字节 U+1F642 正反解码和编码；拒绝 overlong；路径 normalize/join/relative/split；typed INI 经排序 serialize→parse→serialize | COW 不改外部借用源；旧 View 仅比较代数，不再解 data；坏 UTF8 保 offset/scalar；根逃逸拒绝；失败配置候选保原值、长度、代数并报告第3行；canonical roundtrip 字节相等 |
| Buffer/Stream/Binary/FILE | 借用 buffer 定位写、COW，stream seek 越 EOF 写出真实零洞；LE U8/U16/U32、双字 Counter、canonical varU32 与 counted UTF8 编解码；标准 FILE adapter 把22B记录写盘、flush、seek、读回 | 缺口两字节为零；内存 EOF 为 END/0；越界读和非最短 var 编码不改 cursor/out；坏 UTF8 writer 不留下半记录；借用 FILE adapter 先 Release，再由应用 fclose |
| Error/Logger | ErrorObject 查询 code/domain/raw/op，格式字符串包含真实信息；带宽 monotonic 的两条日志写入 buffer sink；两次 flush 后再写第三条并检查淘汰 | 过滤不产生记录；换行被真实 escape；第二次 flush 不重发；只淘汰已 flush 旧记录；View 代数失效，不再读取已淘汰 text |
| Event/代码寿命 | 正式 ScCallbackPin 固定 APP 回调；ONCE 高优先回调内 connect 新项、disconnect 未来项并嵌套 emit | ONCE 在回调前退役，嵌套不再触发；新项只参加嵌套/后续 emit；外层只调用入口允许项；最终 context_drop 恰好3次 |
| Model | 插入两父节点和子节点；property 提交后观察者有意返回 CANCELLED；把子节点移到另一父节点，拒绝循环；Visit0 内新增节点，随后删除完整子树 | 通知失败单独查询，已提交 property 可读；cycle 返回 INVALID且树/代数/通知数保持；snapshot 含虚拟 root0，排除新节点；被删子节点 STALE；真实通知7次，包含四种 change |
| Cache | 两个 stamp 的同 key 资源同时持 lease；缩预算后新候选 BUSY/drop；loader 真 malloc 失败保旧条目；恢复预算后 a/b/c 展示 LRU；invalidate 时保有 lease；先 Release cache 再释放最后 lease | stamp 全32B比较；失败不改 resource/out lease/原 revision；lease 保资源与计费；只淘汰无 lease 的 LRU；最后 ReleaseLease 之后 header 才收回；5次成功 load 与5次 drop 相等 |

输入描述和 query out 都由应用填真实 `sizeof`、version1，保留字段归零。Iterator.position 由库生成且不手改。Container 中只放4B无外部拥有权的数值，浅复制与 drop 计数不会把一个深层资源的拥有权复制两次。回调的 stack context 保持到对象及全部 lease 正常释放之后；代码地址经过正式 pin，未把 context 生命周期误当作代码 pin 的承诺。

Model 的预期 snapshot 顺序是 `0/A/C/B`：B先从A移到C，visit0捕获这个集合，root回调新增的 late 节点不加入当前集合。remove C 只发一次子树 change，B随子树删除；不为每个被删叶伪造独立通知。Observer 返回错误不能让 mutator 假回滚。

## 3. 缓存预算与真实 OOM

单字 key、loader资源4B的当前条目 charge 为78B：72B metadata、1B key、1B NUL、4B resource。探针通过 Query 取得 charge，再据它设两项/一项预算；桶、cache header 与 lease-index 仍消耗 SCCLIB 实际内存，不在这个逻辑预算中。这个检查不能被描述为 PF 容量或整机缓存性能证明。

OOM 分支在真实 loader 回调内请求 `malloc(0x04000000u)`。当前 SCCLIB 每个 arena 最多64MiB，payload加 arena/block 元数据和页取整超过该上限，实际 allocator 返回 NULL/ENOMEM，随后 loader 返回 MEMORY。没有替换 malloc、注入系统故障或大量占满物理页；这是实际容量拒绝的失败候选，不是磁盘故障或 PF 耗尽证据。原条目仍以旧 stamp 命中，failed Acquire 的 out 与 lease 哨兵保持。

另一次预算 BUSY 发生在新资源已实际 load、旧资源仍有 lease 时，因此必须 drop 这个成功创建但不能提交的候选。最终 cache 的用户引用先 Release，header与最后 resource 由活 lease 保持；只使用保存的 closing 指针调用最后 ReleaseLease，不查询已关闭对象、不对已释放 header 调 API。

## 4. 文件字节、marker 与结束条件

成功文件长度为22B，逐字段读取必须还原原值，读回还要匹配独立的固定字节数组：

```text
7f ef be 78 56 34 12 ef cd ab 89 04 03 02 01 ac 02 04 f0 9f 99 82
```

它依次为 U8 127、U16 0xbeef、U32 0x12345678、Counter lo=0x89abcdef/hi=0x01020304、varU32 300、UTF8长度4与U+1F642。双字记录不是尚未实现的 C long long/浮点格式。

每个失败输出唯一 `UTIL FAIL <name>`，status失败还给 actual/expected。正常预期可见：

```text
UTIL BEGIN v1
UTIL containers drops=4/4/3
UTIL text-path-config finished
UTIL buffer-binary-file finished
UTIL error-log finished
UTIL events first=1 late=2 drop=3
UTIL model notices=7 visited=4
UTIL cache load=5 drop=5 oom=1
UTIL PASS checks=<实际数量> heap=<实际页数> modules=<实际页数>
```

`finished`只标控制流已经走到该段末尾，不自行表示 PASS。最终必须没有 `UTIL FAIL`、出现实际 `UTIL PASS`、stdout最后 flush 成功、main返回0。页数与检查数从本次运行读取，不能在脚本中伪造；有错误则输出 `UTIL FAIL TOTAL` 并返回1。编译诊断、映像/map/log摘要与客体输出须分开保存；同次启动中的普通退出必须由外层确认。

应用先 warm 一次小 malloc和实际 stdout，再取 ScSystemMemory 基线；七组对象/FILE/lease正常释放后，比对本人 heap/module页数。warm arena、仍打开的 stdout及运行库仍活，因此这个内部比对只针对流程内遗漏；Root正常 main返回后还要完成库fini、标准FILE关闭、allocator收束及本人PID/代数的模块/页回收。内部平衡不冒充任务退出或全系统池完全回收。

本探针的结束条件是主代理统一构建、实际客体 SCCC 生成/执行、上述真实输出与正常收束成立。只针对出现的具体故障修复/补测；不扩成163函数的理论排列矩阵，也不凭这个小工作流宣称典型大规模容器、UI或整机性能达标。

## 修订记录

| 日期 | 变更与证据边界 |
|---|---|
| 2026-10-15 | 收齐正式SCLIB七组动态工作流、真实FILE二进制输出、回调/模型/cache生命周期及失败保持；仅静态核对，尚未构建/执行 |


修订：2026-10-15，统一stdlib-build-06及guest02/04/05实际SCCLIB/SCLIB/R1/C1..C5、九模式/当前G1/253项通用工作流与精确回收通过；保留三项验证失败、未变输入复用与证据边界，见evidence/m10a2-stdlib，完整目标继续。
