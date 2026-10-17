# Studio 与 Files 产品修复实测

Windows QEMU / WHPX / 1920×1080，使用 HMP 实际输入和 screendump；未使用 computer-use 或 subagent。只检查用户报告的日常操作，不运行历史资源矩阵。

第一轮确认旧 SCAPI.H 程序实际编译并输出 Hello legacy!、打开 C 可直接构建、共享选择器双击进入目录、Files 滚动有界、右键编辑进入 Studio2。第一轮也暴露了默认项目的临时文件名大小写错误，原失败保留。

修复后的第二轮确认：开始页三个入口完整可见；项目默认类型显示 C 控制台程序；保持默认 HelloWorld 名称与模板，F7 实际经过 SCCC/SCLD 成功，Ctrl+F5 输出 Hello, world!，输出末行自动可见。插入一处语法错误后，弹窗及输出都显示 MAIN.C:1 和 expression needs a value；随后正常关闭 Studio 并结束 VM。

第二轮绑定 HOST26/native15/final15。旧头、导航、滚动和编辑路由复用第一轮未改变的行为证据；没有重跑其它应用或已放下的资源事项。review.json 记录实际来源与图片摘要。此记录说明本轮具体缺陷的结果，不改写首发版本的历史结论。
