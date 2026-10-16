# P5 真实跨进程FILE剪贴板

runtime-02实际Windows双盘QEMU通过，六原生库/CORE15直接复用m10a2-p5-file-jobs来源；只在客体中由真实G1生成新的公开接口FILECLIPPROBE。

普通UID1000/SID4的发布者PID4/gen6发布Copy后先退出，另一进程粘贴，真实worker4/gen7 stage3 flags9。新发布者PID4/gen8发布Cut后退出，worker9/gen1 stage4 flags9，只有持久移动完成才Reconcile为空epoch4。三份131079B真实内容均客体逐字节/宿主取回对照一致；两份832B恢复日志CRC与提交记录独立审计通过。最后13830→13830 PF，各池/历史/字体/未归因增量0；原盘/输入保持。

runtime-01原FAIL保留：检查程序误把完成Wait后的child_pid0当作历史身份，并尝试Close已领票对象；按既有original_pid/generation与Wait合同修正。该错误没有被记为系统库缺陷，也没有修改已通过的系统库字节。

index列出实际文件摘要；原运行报告原样保留。主控之后增加桌面布局profile，不能把后续工具源冒称当时相同摘要。真实鼠标FILE拖放、桌面冷启动布局与主要组件/SDK仍待后续，完整目标继续。

修订：2026-10-16，记录实际跨进程发布/退出/粘贴、Cut清理、CRC和owner账。
