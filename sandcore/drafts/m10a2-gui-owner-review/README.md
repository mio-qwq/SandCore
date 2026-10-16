# GUI01 页账与下一 VM 的基点

GUI01 的 `p6-gui-single-final-owner-account` 仍是 FAIL。实际物理页 `13713 → 13918`，差额 `+205`；字体、历史页增量均 0。唯一变化的已列对象池是 `fs.c::native_pool@008ce4a0`，对象 `-1`、池页 `-6`。它表示一个真实 FS 对象后来被释放，不能解释新增页。`205 - (-6) = 211` 只是扣掉已知下降后的算术余项，不是已证明的 cache 归属。

`wait_pages@008cd254` 在本批 core 中唯一，归属 `task.c`。`streams.c` 没有同名统计；两个 `native_pool` 才分别属于 FS 和 streams。基点同一份 resources 已经同时记录 `wait_pages=0`、`finite_wait_heap.pages=1`。`stdlib.Harness.resources` 先读 scalar，`PlatformHarness.resources` 随后逐 HMP 再读 wait 四字段，期间 VM 继续运行。故这不是两个不同 wait 统计，也不是可以重复归属的一页；本次基点不是同一时刻的快照。前后完整 finite wait 记录相同，不能给 205 页增加任何 wait 豁免。原报告没有保存最终完整 resources，也没有保存基点的 PID 页表、Desk 模型、FS 对象路径，因此不能从存量证据补出每 owner 的 211 页来源。

原 warm 只检查 `kernel/desktop.c` 的旧壁纸资源。Ring3 的 SCSHELL 另有 `desk.refresh_pending / icons_pending / store / save_pending`；图标按每轮 4 项工作，`icons_pending` 在一批开始时先清零。因此只看一个零 flag、静止截图或五次相同 PF 都不足以证明原 host 已完成图标工作。这个缺项使异步图标/存活 host 增长成为可检查的候选，尚不是 GUI01 的结论。

`owner_snapshot.py` 是供 Root 已有 `QemuSession / RefreshHarness` 调用的只读薄 helper。它没有启动 VM、发送客体命令、调用函数、修改进程内存或创建测试程序的入口；只使用现有 `vm.qmp / vm.hmp`。`audit.py` 仅离线读取这次报告、真实 core ELF、native 桌面和符号，生成 `REVIEW.json / DELIVERY.json`，也不启动 VM。

先从本次正式 native 行取完整 image SHA、`.sdbg` SHA（或 map SHA），用 `desktop_binding` 读真正的 `desk`。支持 `SCDB1MIO` v1、SCX 绝对 VA、完整 image SHA 绑定，以及 SCLD 正式 `OBJECT VA SIZE desk object=N binding=local` map 语法。map 自身不携带映像 SHA，调用者必须把生成报告的 image/map 两份 SHA 都传入。不能直接用旧 `m10a2_shell_inspect.inspect` 的 regex；它只接受旧的 `OBJECT VA desk SIZE`。

示例中的参数都来自 Root 已核的 candidate，不能填同名 HOST 文件：

```python
from owner_snapshot import desktop_binding, wait_original_host, snapshot

row = candidate.native['native_artifacts']['SCSHELL.SCX']
image = Path(row['path'])
layout = candidate.native['preflight']['task_layout']
binding = desktop_binding(image, row['sha256'], Path(str(image)+'.sdbg'),
                          row['debug']['sha256'], layout)
ready = wait_original_host(harness, candidate.core/'core.elf', layout,
                           binding, original_sid)
if ready['status'] != 'ACTUAL_ORIGINAL_HOST_READY':
    # 将实际观察写报告，停在未验证；不能以 elapsed time 代替 ready。
    raise RuntimeError(ready['status'])
baseline = ready['snapshot']
runtime.baseline = (baseline['physical_pages'], ready['observations'], baseline['resources'])
```

就绪查询核对实际 `(PID,generation,SID)`、可信 kernel MAIN 模块的完整 SHA/长度/READY 标志，以及当前 Desk 的活跃/可见、图标/刷新/保存/拖动终态、SCUI root 没有待画或失败、context 没有进行中的回调。还要求 host 已回到真实等待状态：这样不会暂停在同步 `ScShellIconLoad` 的读循环中而误判。进入本轮隔离 GUI 流程前还要求现有 GC 链为空、没有 loader 登记进行中、FS native 临时源已关闭。最后这个零源条件适用于本轮无其它用户文件工作的启动基点，不是给一般应用制定永久零句柄规则。

就绪轮询只读取轻量模型和原有 owner 链，期间恢复 VM 继续实际工作；0.2 秒只是轮询让出间隔。满足条件后，再用**一次短 QMP stop**同时采集 PF、既有完整 resources、全部活 PID/代数的私有/共享页表、内核栈和登记模块元数据/核验缓冲。记录暂停耗时，默认预算 5 秒；读取错误或超时只返回 UNVERIFIED，并在 `finally` 恢复 VM。不为构造这些观察再编译或运行夹具。

完整 owner 快照只在 `status == ACTUAL_COHERENT_READ_ONLY_SNAPSHOT`、`owner_census_complete == True`、`resume_confirmed == True` 时可用。保留每 owner 的实际 PF 集合；共享页单列，不能在多个进程重复加总。公共对象池节点/叶块仍按全局池账记录，不平均摊到每个 owner。输出额外记录原 ledger 未列的 d2 模块池与各 owner 的模块缓冲，但不宣称覆盖所有内核分配。

结束时恢复同一个原 SID，正常注销测试 SID、收齐 GC 后，再调用同一 `snapshot`，同时传入 `expected_pid / expected_generation`，以同一套页数/资源给现有 `harness.ledger`。**不能只修基点，终账仍配对 `quiet()` 旧页数和稍后未暂停的 resources。** Root 应在外层接线完整快照这一对值；原 runtime 和现有验证脚本本批都没有被修改。

```python
final = snapshot(harness, candidate.core/'core.elf', layout, binding, original_sid,
                 expected_pid=baseline['host']['pid'],
                 expected_generation=baseline['host']['generation'])
if (final['status'] != 'ACTUAL_COHERENT_READ_ONLY_SNAPSHOT'
        or not final['owner_census_complete'] or not final['resume_confirmed']):
    raise RuntimeError('final owner snapshot unverified')
ledger, okay = harness.ledger(baseline['physical_pages'], final['physical_pages'],
                            baseline['resources'], final['resources'], cold_font)
```

FS 的只读对象可能被多个 reader 共享；`native_file_t.owner` 只标记写事务拥有者，不能当只读归属。helper 只发布它的真实字段，并通过 `d2_module.source_object` 指明 loader 核验引用。进一步需要归属现代句柄时，应沿 `sc32_file.c::owners[pid].head → handles.description → descriptions[description-1].object` 加入引用关系，并核 owner generation；不能从 FS 对象上的 owner=0 猜持有者。

当前实现只做离线语法、实际 ELF/SCDB/map 解析和固定摘要核对。没有执行 helper 的 VM 路径，没有新的回收 PASS；GUI01 的 FAIL、源/原盘守恒证据原样保留。
