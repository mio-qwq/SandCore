# GUI03 管理串口失效的窄审阅

当前不能通过已有runtime操作恢复管理通道。`run`/`bind`/`get`均走同一`ManagementClient`；一旦`failure`被设置，`request()`在任何写入前立即抛“串口连接已中断”（`tools/serial_protocol.py:95–103`）。runtime只公开原有业务/输入/截图/取回/结束操作，没有diagnostic/reconnect入口；`harness_configuration()`只临时改变BUILD/PACKAGE/POOL_NAMES，不替换Guest、心跳或transport（`tools/verify_m10a2_ui_platform.py:289–297`）。当前QMP、真实PS/2与画面工作，不代表该Python client状态会自动恢复，也不代表客体死机。

本次只读management日志时，22340 B解出610个有效协议帧：507个PING ACK（kind 0x800a），末尾539后有连续三份seq540的成功ACK，之后没有新序号。debug日志126 B只有启动文本，无STOP/resume；console输出帧仅68份，连整个management日志也远低32 MiB，排除`Guest.receive()`的输出预算上限。两次run/bind记录都约0.031 s失败、输出0 B，符合发送前的永久failure检查。Python的h.run已进入并记录失败case，但该guest命令未经过pipe.write发送。报告仍保留此前真实SID4绑定：runtime只有在h.run成功返回之后才清session，此次异常没有走到该赋值。

最可能是PING一次三轮等待耗尽，迟到ACK之后被pump读到，但心跳已把异常写入client.failure。`ManagementClient.request()`每轮默认3 s、最多三次，重试保持同一序号；`Guest.connect()`的heartbeat每5 s调用checked(10)，任意异常就记录errors、调用fail并退出，不重试恢复（`tools/verify_m9.py:94–107`）。heartbeat失败不设置Guest.stop，pump仍可继续把迟到ACK写入日志。这解释重复540及后续立即失败。该日志没有帧接收时间，也未记录failure原始repr/reader/heart存活状态，所以仍不能确认原始错误究竟是请求TimeoutError、Windows I/O异常或reader故障；不把这个推断改称已经实测的根因。

WindowsPipe区分空读与错误：PeekNamedPipe无数据返回空bytes，Peek/Read/Write的实际错误会抛异常；overlapped I/O有3 s超时与完成取消，没有“空读即EOF”路径（`tools/scserial.py:225–267`）。QMP是独立stdio transport（`scserial.py:275–304,506–522`），其正常不清除管理client.failure。当前日志也没有证据表明命名管道本体已经永久断开。

不应通过重新发业务请求、清failure/errors、换掉活client、重放输入或写host/guest内存尝试恢复。现有client没有reset方法；另一进程不能接管已打开的独占命名管道。即使直接清failure，kernel管理session在无活动3000 ticks后会真实注销（`kernel/management.c:153`），不能冒充旧认证仍有效。真实重连须明确管理会话/新HELLO/序号/reader/心跳生命周期合同；本轮接口不存在，不新增到活helper。

已有非侵入取证出口是Root结束本轮业务并让当前driver实际退出后保存完整stderr。`Guest.close()`会join线程，随后`raise RuntimeError('串口会话错误') from self.errors[0]`（`verify_m9.py:154–165`）；Python cause chain应保留第一份真实异常。不能提前伪设finished，也不能把没有执行的最终owner账标PASS。GUI业务截图和已经真实完成的输入证据继续有效，串口/结束收账边界另列。

`future_transport_diagnostics.py`仅供未来新driver在异常分支记录原始error chain、client.failure、errors、reader/heart存活、stop与pending/sequence。它只读取Python对象，不发送帧、不清状态、不读写VM内存；本轮未接入或调用活helper。超时恢复策略须等原始异常确定，再做窄修，不按当前推断盲目加重试或放宽失败。

`OBSERVATION.json`只是对读取时文件字节的离线解码摘要，不是同步VM快照或最终日志。`PINS.json`固定审阅的源与读取摘要。本稿没有修改主树、活helper、冻结v4或历史产物，没有VM操作、构建或客体输入。

修订：2026-10-16，GUI03仍运行时仅做源码/现有日志只读审阅；原始cause待Root实际退出stderr。
