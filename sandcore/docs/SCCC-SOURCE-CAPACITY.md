# SCCC 源文件元数据容量（M10a2）

原 `Source[64]` 和 `source_once[64]` 会使真实 SC32API 聚合在第65个路径身份
处失败。现将二者合并为按需拥有的动态 Source 表：首批16项，满时倍增，先
申请候选、复制已发布记录，再释放旧表和发布新拥有者。新增记录明确将 once
清零；OOM 保留旧表直到真实路径诊断输出结束，之后由 workspace_release
释放表及原有 source/token/pool/code 四区。成功出口也统一释放这些区域。
其他原有 SCL/SCO 临时页的退出回收规则不改。

只有元数据搬迁。递归 Lexer 持有 source_arena 的稳定正文地址和整数 file
索引；pp_include 在会加载新文件之前已把当前路径复制到局部 candidate；
`__FILE__` 独立复制到字符串池。因此表扩容不改变路径、include 顺序、guard、
`#pragma once`、`__FILE__`、`__LINE__`、map 或 source-line 的语义。
路径仍逐字比较；不按文件内容或宿主规范化路径合并源身份。

Tok.where 使用语句标志1位、源文件索引15位、行号16位。最多可编码32768个
源身份，文件索引为0..32767；第32769项在发布或移位之前明确诊断
`source index capacity exceeded`。容量倍增和字节数乘法先检查；这个上限
来自现有编码，而非新的工程文件配额。SCO v1 的16B source-file 和16B
source-line、SCDB v1 的全局文件下标与对象身份保持原格式，不改变版本。

源码正文稳定 arena 的1MiB总预算、63字节路径和16层 include 深度保留。
其他工作区、SCX loader 上限及旧 ABI 不改。Source 只在本次编译期间拥有，
没有跨编译缓存；已有按路径线性查找算法也没有借此宣称性能优化。

2026-10-16 的定向宿主检查使用当前 SCCC 全部真实预处理/解析/x86/SCO 后端，
只替换文件、分配和输出 I/O。原生 SCX 也由该后端生成，没有使用宿主 GCC
的目标机器码冒充原生编译结果。

| 检查 | 实际结果 |
|---|---|
| 完整当前 SC32API.C → SCO | 223414B，104路径身份、65个真实文件，源码488664B |
| SCO 来源及行表 | 全部字节/FNV核对，2842行，最高源文件索引101 |
| 旧小 C | pragma once、guard、`__FILE__`、`__LINE__`，SCX及map与修改前字节相同 |
| SCCC 自身 → 原生 SCX | load 256454→257350，增长896B；BSS 2602956→2598100 |
| 加载边界 | 257350 ≤ 262108，余4758B；生成器自身更小的262000边界也未改 |
| 一次源表扩容 OOM | 第65路径申请128×76=9728B失败，明确路径诊断；7次申请/7次释放、live0，旧SCO正文保留 |

成功 SCO 的 SHA256 为
`bfbe1a3ce1a7ac593d4f73cc3a7ef5cf038df457cb961f70ac2a78234ac5f458`；
新 SCCC 原生 SCX 的 SHA256 为
`ac7b219535aea51b502d8b7ef609874a4fe7920b8788706349d8585a3ce51a9e`。
它们只对应报告逐项列出的源摘要，不代替随后主树变动的证据。

报告为 `build/m10a2-sccc-source-capacity-agent-check/report.json`，命令与
成功/故障诊断都保存在同目录 commands.log。最后一次 SC32API.SCO.log
故意是 OOM 诊断；成功正文及成功编译记录仍在，不能把日志尾部误读为成功
SCO 被删除或重新编译失败。补丁与精确源 pins 见
`drafts/m10a2-sccc-source-capacity/integration.patch` 和 integration.json。

Windows SandCore 客体内编译、实际运行/退出回收和后续原生自举尚未验证，
由主代理整合后对这个具体改动安排最小回归；不额外扩展理论矩阵。

修订：2026-10-16，取消64项源元数据门槛；真实宿主后端四项定向检查通过，保留15bit格式/源码arena/SCX容量及客体待验边界。
