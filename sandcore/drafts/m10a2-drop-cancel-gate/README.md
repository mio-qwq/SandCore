# Drop 取消门控补充

这是冻结 Drop 稿之后的独立两 hunk，只修改新增私有 `user/scui/scui_drop_deferred.inc`。旧稿、主树和既有证据不改；没有构建或客体运行。应用顺序是先整合 `m10a2-drop-source-refresh`，再应用本组精确补丁。

`ui_d2_source_work` 的确没有优先检查 `d->cancelled`。但不能据此认定普通显式取消一定等到下一次 STREAM：`ScUiDragCancel → ui_drag_close` 不只置位，还释放基础引用；基础引用为最后一个时，`ui_object_release → ui_object_finish → ui_drag_dispose` 当场关闭 channel。当前 UiDrag 的其它 retain 只来自 pump 对 current/next 的临时引用，pump 每轮即使 work gate 不成立也无条件释放；回调内取消通常也在该轮 dispose。因此本组修的是明确的门控遗漏，不伪造已观测的挂起故障。

两 hunk 的改动：

- 增加私有 `ui_d2_source_retired`，只核本人 source/root 节点是否缺失或已退役。deferred source_step 在任何读写之前据此返回 CANCELLED，交给原 pump 的 close/dispose 路径。
- source_work 首先放行 cancelled，其次放行真正缺失/退役的 source/root，最后才查短握手过期和 channel readiness。这样清理本身就是可运行工作，Wait 同样返回0，不依赖新的 STREAM 才到达取消分支。没有把隐藏、失焦、disabled 当退役，没有新定时器、轮询目录或公开接口。

只读节点销毁链：`ScUiNodeDestroy` 持有节点，清输入，再 `ui_node_retire`；退役先标记 RETIRED 并从 `c->nodes` 删除，随后叶到父发送销毁通知。活跃回调/子节点引用可延后 `ui_node_clear`；clear 调 `ui_drag_node_destroy`，按 source 或原 root 精确匹配，执行原 `ui_drag_close`。本组在“已退役、clear 尚未执行”的窗口也能使 deferred step 终止；不额外释放引用，不对 `cancelled` 直接赋值，不改变原 clear 顺序。接收方节点由原 peer_step 与 drop-node 清理链处理，本组不改接收方。

`PINS.json` 固定前一组 DELIVERY、该私有片段前像与结果、两 hunk 补丁及本次只读引用的 `ui_base.c/ui_tree.c/ui_data.c` 摘要。生成器只核唯一锚点、精确差异和冻结输入不变，没有调用 C 编译器。`SOURCE_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED` 继续有效，不增加验收矩阵。

修订记录：2026-10-16，Drop 取消与退役源门控的局部补充，保留原引用释放因果和验证边界。
