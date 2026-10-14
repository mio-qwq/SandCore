# SCL 应用根正常终止方案

2026-10-15，**SandCore M10a2 — The Application Platform Era**。R1已统一构建、由客体SCCC生成57040B/12导出根库，并实际执行借用main缓冲的return37/exit38、init退出42、递归异常1及精确回收，见[实际证据](../evidence/m10a2-stdlib/README.md)。64B CRT、内核、冻结SCAPI.H与此前P0证据保留；本批不代表完整M10a2结束。

合同来源为 [SCCLIB-DESIGN.md §10](SCCLIB-DESIGN.md#10-正常终止main-返回与异常)。本方案补足受控 continuation、借用栈缓冲、init 事务中 exit、callback 寿命和独立 runtime 的实际边界；不宣称公开 setjmp/signal 或完整 hosted C 已实现。

## 1. 改动前控制流（历史分析）

- `SCLROOT.C` 聚合格式校验、scldr、platform 和 application 源；根没有自己的 main、init/fini、外部 imports 或 dependencies。
- `scl_application.c::ScApplicationStart` 当前建立 runtime、借入根、Open 依赖、Resolve 全部导入、回填应用，再直接调用 main；main 返回后直接 Destroy。尚无 prepare / atexit。
- `scldr.c::link_module` 在 `runtime.busy=1` 的 Open 事务中调用 init；init 成功才进入 initialized 链、ready=1。rollback / Destroy 先逆初始化链 fini，之后释放映像。borrowed 根不自行卸载，最终由进程页回收。
- `scl_platform.c::invoke` 是普通 Ring3 cdecl 调用，不接收恢复上下文。平台分配和文件桥不依赖 SCCLIB，因此根的终止栈可用原平台页分配。
- GCC / SCCC 生成的动态 SCX 都使用同一 64B CRT：取得 SYS0x283 bootinfo，cdecl 调用 ScApplicationStart，返回码交旧 SYS_EXIT。该字节桥不需要改变。
- `ScProcessExit` 直接调用旧 SYS_EXIT；内核记 exit_code、置退出并分步清理，切走后回收页 / 内核栈。不会执行用户 atexit、stdio 或 fini。
- SCCC inline asm 当前没有 jump / ret，也会生成普通 C prologue / epilogue。只添加一个 asm jump 白名单无法正确换栈；不允许猜 EBP 或从 C 函数随意改 ESP 后 return。

## 2. 兼容与新增公开接口

原九个 ROOT exports、签名、状态和数据布局全部保留：ScApplicationStart / Runtime 以及 ScRuntimeCreate / Open / Close / Symbol / Resolve / Query / Destroy。原 SCAPI.H、SYS_EXIT、0x283 bootinfo、ScRuntimePlatform 和 ScRuntimeStats 不扩写。

拟在 `SCLDR.H` 增加三个真实 cdecl 声明，SCCLIB 使用独立 manifest imports；普通应用仍只包含标准头：

```c
int ScApplicationRegisterPrepare(void (*prepare)(void));
int ScApplicationPinCallback(void (*callback)(void));
void ScApplicationExit(int status);
```

ROOT 保持 ABI major=1，拟升级 minor=1、library_version=2，exports 从九个增为十二个。新 SCCLIB 依赖 `SCLDR.SCL` ABI1 / min_minor1 / min_version2；旧 min_minor0 / min_version1 应用和库仍匹配。系统名称和 SYS/LIB 权限规则不变。根仍无 imports/dependencies/init/fini，新增助手不借此获得 SKM / Ring0 权限。

RegisterPrepare / PinCallback 返回既有 SC_SCL 域：FORMAT=-1、RANGE=-2、MEMORY=-6、HANDLE=-9、BUSY=-10。状态与参数错误不修改现有注册或引用；没有新 SC32 errno 含义。ApplicationExit 实际永不返回，合法路径把完整 signed32 status 交 CRT / 旧进程退出码；非法状态有固定小诊断并以 EXIT_FAILURE=1 异常结束，不把它当正常第二次退出。

### 2.1 Prepare 注册

SCCLIB.init 完成自身 allocator、FILE 和至少32个登记槽准备后，在最后调用 RegisterPrepare(自身 void prepare(void))。根确认：

1. 当前是本进程自动应用 ROOT 的唯一 runtime，不是独立 ScRuntimeCreate 对象或另一个加载的 ROOT 副本。
2. loader 实际处于 init invoke，current_initializing 正是 SCCLIB.SCL；不信任调用方传入的模块编号。
3. prepare 非 NULL，地址在该模块真实 X 段内；不要求是 export，接受 static 内部函数。
4. 尚无另一个 pending / committed provider；重复或普通 main 直接注册返回 BUSY。

先申请真实128KiB终止栈候选，成功才在当前 ScLoaded 挂 pending hook；此时不 pin provider、不发布应用 hook。init 返回0后同一 loader 提交点将 pending hook / 栈转为 committed，pin provider；init 失败、事务 rollback 或 finalize 在释放 provider 映像前先解绑并回收未使用候选。失败不能留下没有注册理由的 resident 引用。

正常终止已切到终止栈之后，不得从 provider fini 释放正在执行的栈；该栈归应用根而非 SCCLIB，最后恢复旧 ROOT 栈后单独归还。SCCLIB 的失败 init 仍负责撤销自己的外部副作用；未成功 init 不调用其 fini。

### 2.2 Atexit callback pin

atexit 保存的可能是库内 static 函数地址，未经过 ScRuntimeResolve；只 pin SCCLIB 不能保护该地址。PinCallback 只允许自动应用 RUNNING / PREPARING，且该 runtime 不在 loader busy 事务。

- 应用函数地址必须落在已验证 SCD 的 initialized 正文 `[USER_BASE+64, metadata_va)`；拒绝 CRT、metadata、BSS / heap。当前 SCX 未提供 text 分段，此范围是该格式现有函数域，不宣称完整 CFI 或 NX。
- 模块地址必须落在本人同一 runtime 的 READY 模块 X 段；其真实 metadata 已验证。支持内部 static 地址，不只接受导出地址；成功设置现有 module.pinned，依赖 edges 保持其闭包到终止。
- NULL 返回 FORMAT，不属于上述域返回 RANGE，没有自动应用 runtime 返回 HANDLE，LOADING / busy / FINALIZING / FINISHED 返回 BUSY。失败不 pin 任意候选。

atexit 先分配候选节点，再 PinCallback，最后挂 LIFO 链。pin 失败回收候选；链和既有登记不变。SCL init / 装卸事务内登记明确失败，不把随失败 init 回滚释放的函数留到将来的 prepare；这不是静默丢弃成功登记。允许 PREPARING 中的新登记进入尚未执行链头；FINALIZING 之后不再登记。

## 3. 两栈控制点

仅恢复原 ROOT ESP 再调用 prepare 会复用 exit 之前的 main / 库栈，例如 main 的自动数组仍作为 setvbuf 缓冲，而 exit 的回调 / fini 可能覆盖它。此路径不能称完整正常终止。

采用单调用 runner：在原 ROOT 栈保存 continuation，entry thunk 在该栈运行依赖 bootstrap / main；正常返回或显式 exit 都进入自写汇编 landing，**在任何新的 C 清理调用之前切到终止栈**。ROOT finisher 在独立栈调用 prepare / fini，原应用栈内存一直保留。finisher 返回后，汇编恢复 ROOT caller 的栈 / callee 状态；ROOT 再归还终止栈并返回 CRT。不得在正在使用的终止栈上 free 该栈。

runner 覆盖整个 dependency / relocation / main body，不能只在 main 之前才 arm；后续 init 的 exit 必须能够离开真正的 invoke / Open 调用栈。SCCLIB hook 成功提交之前，没有可使用的终止栈，ApplicationExit 为明确异常失败。没有 SCCLIB 的应用保持现有 main / fini 行为，不预分配128KiB；runner 无终止栈的普通返回可在原 ROOT 栈完成原清理，新增正常 exit 服务要求 committed provider。

128KiB 是每个启用 SCCLIB 自动应用的真实终止栈预算，与当前 SCX 最大普通栈量级一致，非任务或回调数量限制。页从原平台分配，候选 / 已提交 / 使用中 / 已释放状态明确；申请失败发生在 SCCLIB init 提交前，不把 main 跑一半后 OOM 当正常退出。实际页成本和暖态归还由统一定向验收记录。

## 4. 私有编译器 / GCC adapter 合同

两个名字只在 ROOT 私有头声明，不安装到公共标准头，不进入 ROOT exports。SCCC 识别真正已声明的特定 call，生成完整 i386 adapter；GCC 用同签名自写 naked basic-asm 实体。共同 C 控制逻辑不改 ESP，不含普通 prologue 后的非局部 return。

```c
typedef struct {
    unsigned int caller_sp;       /* 0：runner保存四个callee之后的ESP */
    unsigned int caller_bp;       /* 4 */
    unsigned int caller_bx;       /* 8 */
    unsigned int caller_si;       /* 12 */
    unsigned int caller_di;       /* 16 */
    unsigned int landing_pc;      /* 20：本adapter真实landing */
    unsigned int termination_top; /* 24：有效候选末端；0为未启用 */
    unsigned int finish;          /* 28：ROOT finisher */
    unsigned int context;         /* 32：ROOT私有应用状态 */
    unsigned int status;          /* 36：完整32位退出码 */
    unsigned int reason;          /* 40：0普通entry return / 1显式exit */
    unsigned int armed;           /* 44：一次控制点 */
} ScRootJumpI386;                 /* 恰好48B，仅本人R3地址 */

int __scl_root_run(ScRootJumpI386 *jump,
                   int (*entry)(void),
                   int (*finish)(void *context),
                   void *context);
void __scl_root_unwind(ScRootJumpI386 *jump);
```

两函数入口接受 ABI4；run 正常仅返回一次，EAX 为 finisher 返回值，EBX/ESI/EDI/EBP 恢复 caller 原值，ESP 回到普通 cdecl caller 状态。ECX/EDX/EFLAGS 可破坏，DF 保持0；无浮点 / SSE / libc 依赖。unwind 实际无返回，不作公开 setjmp 或 returns_twice。

### 4.1 Run 的机器步骤

1. 依次 push EBP/EBX/ESI/EDI；此时 jump/entry/finish/context 分别在 `[ESP+20/24/28/32]`。保存当前 ESP 与四个 callee 的入口值，保存真实 landing PC、finish/context，armed=1。
2. landing PC 用 adapter 内 call/pop 加标签差计算，避免将固定宿主 VA 写入 SCL。jump 不提供给应用；内部地址按实际 ROOT 映像生成。
3. ESI 暂存 jump（入口值已经保存），对齐 ESP 到16B后 call entry；entry 是 ROOT thunk，不是直接裸 main。当前 ROOT / 应用栈保持与既有 ABI 相同区域。
4. entry 正常返回后，将 EAX 写入 status，reason=0、armed=0；不调用 C、不在应用栈新建清理帧，直接进入 landing。
5. landing 始终以ESI=jump读取 termination_top / finish / context；有效顶端向下16B对齐，留12B垫片、push context、call finish，保证调用前16B对齐。finisher 按 cdecl 保留ESI，不能恢复入口SI后丢掉控制块。
6. finisher 返回后保存其 EAX，不再引用库帧；ESP=caller_sp，pop EDI/ESI/EBX/EBP，ret 返回原 ROOT C caller。原 ROOT caller 此时才释放终止栈。

未启用终止栈仅适用于普通 entry 已返回、没有 committed SCCLIB provider 的原清理路径；可用已保存 caller_sp 对齐建立 finisher 帧。显式 unwind 不允许用这个回退分支。

### 4.2 Unwind 的机器步骤

ROOT ApplicationExit 先校验状态和真实 init invoke 边界，设置 jump.status 和退出来源，保持 armed=1；随后普通 cdecl 调用 unwind，jump 在 `[ESP+4]`。

unwind 取得 jump 到 EDX，核对 armed=1 / termination_top 非0，设置 armed=0、reason=1，先恢复 ROOT caller 的 BP/BX/DI，ESI=jump，并跳真实 landing_pc。ESI 在 landing / finisher 期间持续保存 jump，入口 SI 最后从 savedSP 的保存字恢复。**不沿 SCCLIB / init / main 帧 return，不先在旧应用栈运行 C finisher**。landing 按上述独立栈步骤继续。私有适配被错误调用时 UD2 / 明确异常终止，不能继续使用无效 continuation。

SCCC 必须记录 builtin 的完整内存副作用、精确参数原型和无返回语义；不得按普通未定义 extern 生成 SCL imports。两个 helper 在 ROOT text 内有真实机器正文，internal call / landing 相对地址处理必须与 GCC 版本一致。GCC naked 函数只含 basic asm；不能使用输入约束要求编译器插入的隐藏 C 帧。

## 5. 根状态与终止顺序

应用根私有状态为 NEW → LOADING → RUNNING → PREPARING → FINALIZING → FINISHED；失败有 FAILED，显式逃逸另记录 INIT / MAIN 来源。有效 Start 一开始 latch，重入或已结束后的第二次 Start 返回1，不再次回填 / 调 main / Destroy。application_runtime 在成功 adopt 后、所有依赖 init 前可见，保持到全部 fini / Destroy 结束后才清零。

正常 main 返回：ROOT entry thunk 返回保存结果 → landing → PREPARING 调一次 committed prepare → FINALIZING → 逆成功初始化顺序 fini → SCCLIB.fini drain/flush/close所有FILE、处理临时资源、回收回调节点和 allocator → SC32API.fini → runtime 映像 / 元数据释放 → FINISHED → 原 ROOT 栈释放终止栈 / 工作数组 → CRT 原 SYS_EXIT。

SCCLIB.prepare **只执行 atexit**。每节点先脱链再调用，重复函数地址按登记次数各调用一次；执行期间新成功登记插入待执行链。prepare 期间 stdout/FILE/allocator 和全部 callback provider 保持存活。依赖者 fini 随后仍能使用 SCCLIB，故不能把stdio / arena提前放到 prepare 回收。

SCCLIB.fini 不执行 atexit 第二次；逐流尽力 drain、真实 flush、close，首个失败保留/诊断并继续其余流。不得 close/reopen 假 flush，不重复写已成功 prefix。输入流也关闭，自有 buffer / FILE 释放后才归还 allocator arena。保留明确 main / exit 状态，不把非零状态改为成功；IO诊断和页回收不代表持久化成功。

PREPARING 时禁止同一自动 runtime Open / Close / Destroy；Resolve / Query 可按已有安全合同读取 READY 对象。PinCallback / 新 atexit 仍允许。FINALIZING 保持原 loader busy，fini 中普通 stdio / allocator 可用，其 graph 重入返回 BUSY。公开 Destroy 对活自动应用 runtime 只允许 ROOT 内部终止授权；普通 main 不可拆掉自己的 active 图。独立 runtime 的现有合法 Destroy 前置条件保持。

## 6. Init exit、失败和非法调用

| 情形 | 唯一处理 |
|---|---|
| main 普通返回 / RUNNING 正常 exit | 正常 prepare → fini → stdio / allocator → CRT，完整退出码 |
| SCCLIB 已成功 init，后续真实用户 SCL init 中 exit | 支持受控 init 逃逸，终止整个自动应用；不回 Open，不运行 main；成功初始化 prefix 正常 prepare/fini/stdio |
| SCCLIB 自身尚未成功 init / provider 未提交 / 无终止栈时 exit | 固定诊断 + 原 SYS_EXIT(1)，不返回 C，不装卸忙图 |
| atexit / prepare 内再 exit；fini 内 exit | 超出本阶段保证的递归终止，固定异常FAILURE1；不第二次遍历、不继续回调、不递归 Destroy |
| runtime busy 但不是真实 INIT invoke，如Close/fini或平台读/保护 | 异常FAILURE1，不擅自解锁一般事务 |
| init 返回非0 / 缺依赖 / 缺导入 / 终止栈OOM | 不运行 main，不执行用户 atexit；原事务 rollback 成功init prefix，完整资源回收，退出失败 |
| 独立runtime / ROOT副本没有自动应用上下文 | Register / Pin 返回HANDLE；Exit无 continuation，异常FAILURE1，不借它控制其它 runtime |
| 错函数域 / NULL / 重复prepare注册 | FORMAT/RANGE/BUSY，无候选提交、无新增pin |
| _Exit / abort / ScProcessExit / kernel terminate /真实异常 | 原低层回收，不保证atexit/fini/stdio；abort首批不称完整SIGABRT |

### 6.1 为什么 init 逃逸可以安全清图

只允许 loader 标记的真实 init invoke：prepare 已完成格式/映像/重定位/权限，ScLoaded 已完整挂入模块表，之前成功 init 已在 initialized 链；当前 init 尚未返回成功，所以不在该链。跳出整条调用栈之后，ROOT finisher 确认 INIT 来源和 current_initializing 边界，清已被放弃的 invoking / busy，进入受控终止；没有一般的“用户要求force解锁”接口。

终止整个应用不再继续该 Open 事务，不读取已弃 C checkpoint 局部变量。先调用 committed prepare，再仅沿成功 initialized 链 fini；中断的 init 不调用 fini。所有映像 / metadata 保留到全部 callbacks 完成，最后释放整个 modules 表，不依赖半遍历的 external / edges 继续执行应用。init 的额外私有页和内核句柄仍由进程 owner 回收兜底。不能在 init 栈上调用公共 Destroy 或把 busy=0 作为任意重入捷径。

Init 返回错误是另一条路径：按原事务 checkpoint rollback，不执行正常 atexit；provider pending / committed hook 必须在对应模块 finalize / 释放前解除。普通 init 内 atexit 返回失败，不留下将被 rollback 卸载的 callback。已成功 SCCLIB 所属更早事务仍保留到 ROOT 收束，不因为后来另一个 Open 失败就伪造 provider 已卸载。

### 6.2 独立 runtime 边界

ScRuntimeCreate 仍创建独立空图，不自动成为 process main、不拥有 exit continuation。自动应用 root 中再建独立图不能注册或借用当前进程终止 hook；它的宿主在没有库调用帧时正常 Close / Destroy。Start 用已有 SYS0x283、ECX=0 取得本人 cached bootinfo，精确核对本函数 entry 与全部8字段；不重新扫描应用或依赖。独立 ROOT 副本不能凭伪造自洽info冒充 kernel 固定自动根。

新 SCCLIB 的正常进程终止 profile 要求自动 ROOT；无该上下文的 SCCLIB init 应明确失败并撤候选，不能让 exit 成功链接但跳过全部标准收尾。现有独立 loader / 无SCCLIB库的有效调用保持。未来若提供显式受控应用宿主，应增加正式 opt-in 运行入口；不由 Create 或旧忽略字段暗中安装进程终止能力。

## 7. 最小实施文件与必要证据

ROOT owner 修改 `user/runtime/SCLROOT.C`、`scl_application.c`、`scldr.c`、`SCLDR.H`、`SCLROOT.json`，新增私有 `scl_termination.c` 与 `SCLJUMPPRIVATE.H`，GCC naked 分支在同一私有源；同步 SCL / 本规范，当前实施与构建接线由root协调。ScRuntime 私有 invoking / hook字段不扩公开结构。

compiler owner 实现上述两个明确名称的 intrinsic 和真实能力宏 `__SCCC_ROOTTERM__` / emitter，私有声明在 SCLJUMPPRIVATE.H，缺能力的SCCC编ROOT明确#error；root统一接构建依赖和 SDK。SCCLIB owner 只实现自身 prepare / fini / atexit / exit，依赖版本和三函数 imports，与根候选提交顺序一致。实现收齐后统一构建和定向验证，不能把源码状态当作P0历史产物的新证据。

后续一组定向程序即可核对 main return / explicit exit / 后续 init exit / 递归exit安全失败、atexit一次/LIFO/动态callback pin、fini继续写stdio和借用 main 自动 setvbuf 缓冲，读取真实文件与128KiB页归还。GCC / 客体 SCCC 用同私有adapter合同生成执行，旧九导出和旧 SCAPI摘要保持；不扩大全矩阵、重复整盘或以设计当PASS。

## 修订

2026-10-15：只读核对 ROOT / loader / platform / CRT / SYS_EXIT，提出原九exports兼容、三个新增接口、48B私有双栈runner、init逃逸与callback pin、stdio/allocator收尾和独立runtime边界；设计未实现，等待GUI验证冻结点后再实施。

2026-10-15：P0检查点保存后收齐实际ROOT双栈adapter与C生命周期、init/回滚hook、静态callback pin及cached根身份核对；原9exports只增3，CRT/SCAPI保持，ROOT源码静态收齐，未构建或客体验证R1。


修订：2026-10-15，统一stdlib-build-06及guest02/04/05实际SCCLIB/SCLIB/R1/C1..C5、九模式/当前G1/253项通用工作流与精确回收通过；保留三项验证失败、未变输入复用与证据边界，见evidence/m10a2-stdlib，完整目标继续。
