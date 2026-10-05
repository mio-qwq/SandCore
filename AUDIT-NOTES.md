# SandCore 审计笔记（第一轮 · 部分收集，非正式报告）

> 2026-10-02。mio 指示：本轮只收集信息不写正式报告；M8 全部写完后再做
> **真正的完整审计**（以届时状态为准，本笔记仅作底稿与线索）。
> 状态：8 个领域各派 1 个只读审计 agent，因并发限额仅 2 个完成/在跑。

## 一、已完成：用户态与系统调用面审计（agent_3b287b42，完成）

完整报告已在其输出中，核心结论摘录（详细证据见原文，输出文件:
`C:\Users\Administrator\.zcode\cli\agents\sess_ce0bcc39-6a9d-44fe-b611-14651dd58e4b\agent_3b287b42-47f7-465d-88bf-a5e097c58c2d\output.txt`）：

### 高价值发现（P1）
1. **NUI 应用空闲轮询放大**：user/NUI.inc:381-399 每 tick 6-7 次系统调用
   （sc_tick+pointer_peek+info+display+theme+key_peek+yield），静止应用
   ≈700 syscall/s，4 个 GUI 后台 ≈2800 次/s。建议：低频项加代数版本号
   （theme 已有代数机制可参照）或新增聚合快照号（新号不破坏旧 ABI）。
2. **每个 NUI 应用启动即 sc_alloc(1920*1080*4)=8.29MB 私有堆**（NUI.inc:282），
   与实际模式无关；NTASK=8 全 GUI 峰值 66MB，逼近 64MB 每任务堆上限。
   建议：按 sc_display 返回的模式分配，resize 再扩。

### P2 摘录
- M8 原生 shell.c **未上盘**（Makefile USER_APPS 无 shell，磁盘仍是 asm 版
  4629B）→ M8 用户空间核心能力（作业票据/PATH/cwd/终端附着）对用户不可达，
  且与 docs/USERSPACE.md 表述脱节（违反 AGENTS 文档同步约束）。需确认意图。
- 分发器 50 个 case 手写三段样板（ustr + paging_user_range + frame 赋值），
  寄存器映射反直觉（ECX=frame[10] EDX=frame[9]），漏写即漏洞——本次逐案核对
  **未发现现行漏洞**，但建议抽 sys_str/sys_buf helper + 跳转表。
- ustr() 逐字节走页表（task.c:283-287），最坏 4096 次两级查找/字符串。
- FSDIR 输出解析器 ring3 有三份拷贝（files.c:50 / NUI.inc:453 / cli.c:37）。
- UI.inc（318×170 冻结）与 NUI.inc（ARGB）双运行时并存，lumen/world/race 停留旧版。
- SCAPI.H 膨胀为 libc+调色板+主题+UI helper 混合体；page()/wait_escape() 硬编码
  304×150，高分下生成错误布局但住在"只增不减"的头文件里。
- SKM 模块 = 零环可信代码 + module_api_t 无版本位（M8 表尾 28→36B 靠模块侧自觉）。
- userspace.c 的 copy()（容量不足整体失败）与 SCAPI.H 的 copy()（静默截断）同名反义。

### 正面结论（同样重要）
- **"只增不减"承诺合规**：新能力一律新号，旧寄存器未重新解释，四方
  （SCAPI.H/常量/分发器/SYSCALL.md）无漂移。
- **未发现可被 ring3 利用的内存安全洞**：ustr 4096 上限、paging_user_range
  先余量后逐页、PTE US 位校验、拒绝 kernel_pd，闭环完整。
- SCAPI.H 内联汇编桥的 clobber 审查**正确**（EBP 先存后装、四寄存器 LIFO、
  memory+cc 覆盖副作用）。
- s3c 自举达成：G1→G2→G3 逐字节相同（docs/C.md:43-50, build/m7-compiler/results.json）；
  自编译产物 1.8 倍体积（无优化器，可接受）。
- 量化：NUI 活动帧 ≈10-11 syscall/帧；ring3 重复未共享逻辑 ≈400-600 行源码 /
  100-160KB 机器码（bin/ 的 19 命令共用 7886B cli.c 是正面对照）。

## 二、 mio 本人已读部分（display.c 全文 / wm.c 合成路径 / main.c 主循环）

1. **display.c**：Bochs DISPI LFB（PCI BAR0 探测，不硬编码显存地址 ✓），
   六档模式至 1920×1080×32，`display_present_rgb` 用 `rep movsl` 整帧搬运
   （高效✓）；缩放 100/150/200%；SYS/DISPLAY.CFG 严格校验（display_config_check
   防前缀宽松匹配，保存合同独立）；15 秒安全回退用 PIT 截止。
2. **wm.c 合成**：已有脏矩形局部提交（gfx_swap_rect）、纯光标移动增量路径、
   故障卡片优先命中、wm_frames/ticks 统计（Monitor 接入）。
   **疑点 A**：`wm_request_compose(){dirty=1;damage_full=1;}` —— 所有调用方
   （窗口开/关/文字/图标/EXEC）都走全量损伤，局部机制被架空；鼠标纯移动走增量
   ✓。**疑点 B**：全量分支 `if(GFX_W==320 || !desktop_wallpaper())scene_paint();`
   —— scene_paint 硬编码 320×200，1080p 下壁纸如何铺满需查 desktop_wallpaper()
   （desktop.c:281）与照片缩放路径。
3. **main.c 主循环**：已高度打磨——task_idle 空闲记账、合成期间 task_render_hold
   暂缓任务切换（防 8042 FIFO 丢键）、时钟只损伤任务栏、wm_key_input 聚焦路由。
4. **数据盘 64MB**：build/ 有多个历史遗留 img（M7-bootstrap/M8-before-* 共 ~90MB
   垃圾），且 M8 引入 64MB sanddata —— 磁盘与内存容量账本需下一轮核实。

## 二.5、已完成：安全审计（agent_a5b35f11，完成）——发现 2 个 P0！

完整报告在其输出文件: C:\Users\Administrator\.zcode\cli\agents\sess_ce0bcc39-6a9d-44fe-b611-14651dd58e4b\agent_a5b35f11-7f72-4075-bda0-ecf074b49394\output.txt

### P0（可提权/破坏内核）——两处改动即可消除
1. **TSS I/O 位图基址未初始化（=0）**：task.c 的 tss[104] BSS 清零后，偏移 102-103
   的 I/O 位图基址恒 0 → 位图"落"在 TSS 本体上几乎全 0 → ring3（IOPL=0 时按位图
   逐位放行）可直连 ATA 0x1F0/PIC 0x20/PIT 0x40/8042 0x60 等端口。十几条指令即可
   裸写磁盘超级块、屏蔽全部 IRQ、8042 复位整机——FS 层一切保护被釜底抽薪。
   修复一行：tss 初始化补 *(u16*)(tss+102)=104;（基址≥limit ⇒ 全端口 #GP）。
2. **SYS/MOD/*.SKM 对 ring3 可写 + 开机以 CPL0 执行**（module.c:48, fs.c:80 只保护
   SYS/CORE）→ 任意 ring3 程序 FSWRITE "SYS/MOD/PWN.SKM"（合法 SKM1MIO 容器），
   重启后攻击者即拥有内核（持久化提权后门）。修复：SYS/MOD 加入 protected()
   或 SKM 加签名校验。

### P1/P2/P3 摘录（完整 15 条见原文）
- P1: 除 SYS/CORE 外全部系统配置（ENV.CFG 的 PATH/MENU.CFG/desk/*.lnk/FONT.SCF/
  DISPLAY.CFG/THEME.CFG）对任意 ring3 可写 → PATH 劫持 + 界面欺骗（信任锚失守）。
- P1: SandFS 无所有权/一致性——正在被分块读取的文件可被另一任务静默截断/替换。
- P2: SYS_USERALLOC 无配额（单任务吃光物理帧后全系统 OOM）；DBGEXEC 暂停任务可
  占满 7 个任务槽；syscall 全程 IF=0 且 FSWRITE 最大 64MB 逐扇 PIO+每扇 FLUSH
  （键鼠/心跳冻结型 DoS）；DISPLAYAPPLY/THEMELOAD 无节流（体验级 DoS）；
  write_meta 先写超级块后写目录且无回滚（掉电 → 全卷判无效 → 用户文件不可见）。
- P3: 任务名/状态对任意任务可读；kfree 不查双重释放；无 SMAP/SMEP 纵深（校验与
  使用分离模式）；INT3 门 DPL=3 暴露面；EXCRETURN 特权剥离与 ATA 串行纪律核验通过。
- 正面：ring3 异常隔离（暂停卡片）质量高；paging_user_range 逐页 US 校验扎实；
  窗口句柄 owner+稳定 handle 双校验；"只增不减"四方无漂移。

### 威胁模型一句话
单用户、无凭证、内核即 TCB；ring3 间仅分页部分隔离；磁盘上除 SYS/CORE 外一切可写；
最危险三件事 = TSS 端口后门、SYS/MOD 提权、界面信任锚可改写。

## 三、未完成 / 待下一轮

- **进行中未完成**：安全审计（agent_a5b35f11，威胁模型：恶意 ring3 程序；
  其输出文件同目录 agent_a5b35f11-.../output.txt，完成与否下轮确认）。
- **未审计领域**（原计划 8 个，本轮完成 1 个）：引导链与盘镜像（boot.asm 逐扇读
  的启动耗时、build/ 90MB 遗留镜像）、CPU 热路径（IRQ0→sched_pick→CR3 频率）、
  内存管理（PF-Bitmap 扫描、堆碎片、1080p 帧缓冲 8.3MB 的分配账本）、
  图形合成带宽（1024x768x32 与 1080p 的每秒写字节数）、存储（ATA PIO 逐扇 vs
  多扇 DRQ、64MB 盘目录一扇区 12 文件上限）、WM/输入延迟链。
- **下一轮审计要求**（mio）：以届时最新代码状态重新审，不沿用本笔记结论直接
  下判断（项目持续变更）；可重查本笔记列出的疑点是否仍在；M8 完整写完后再做
  正式报告（带评分）。

## 四、 mio 的既定方向备忘（本轮确认，不要推翻）

- 窗口改大小/最小化：计划加入（M6b/M7）。
- 创建窗口等 API 要详细写入 OS 自己的 API 文档（ring3 化后正式化，
  现有 docs/SYSCALL.md 已起步）。
- panic 的 VEC 0x2A 显示重叠 bug 已修复（数字移到 x=72/80，M5 收尾时）。
- 用户实测"卡慢不跟手"是 M8 硬验收项：要真实的输入/拖动/键入延迟统计，
  禁止用名义 FPS 或降分辨率冒充（M8_GOAL.md §2）。
