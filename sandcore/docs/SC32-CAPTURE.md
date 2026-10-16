# 受管进程输出捕获

2026-10-16。`SC32CAPTURE.H` 的 `ScProcessSpawnCapture(command,out)` 由SC32API ABI1.4/version6提供；out为32B/version1，包含真实ScJob以及现代ScFile stdout/stderr。新SYS300 FILE selector20B。stdin为真正NULL READ/EOF，不是PTY或交互Shell。

孩子仍只用旧标准fd0/1/2，父的两个读取端是动态native reference与现代FILE，不占旧16fd。FILE私有description中native_endpoint独立于standard索引；STANDARD只解释0..2，不以standard=3制造越界。新公开SC32_FILE_STATE_NATIVE_ENDPOINT解释非标准native流的endpoint_kind/capabilities，旧STANDARD位及布局保持。

构造前验证完整输出；现代FILE、管道、引用或spawn失败都回滚，孩子只有全部准备好后才可运行。普通owner/session/作业代数与原streams权限保持，不能猜PID读别人输出。读取是非阻塞AGAIN/EOF，取消和领取沿ScJob；Wait完成消耗票据，原身份取original_pid/child_generation，不能重复Close。关闭/父退出均按真实引用归还资源。

同源32位streams/objpool宿主模型已覆盖旧3..15 fd全满时24个并发capture、独立stdout/err、AGAIN/EOF、错误owner、spawn/九个物理页失败点和父退出全回收。主树streams/FILE与Studio已HOST构建；真实编译stdout/取消/客体资源账待验，不把模型当作QEMU。

修订：2026-10-16，动态native输出、旧fd兼容/回滚和实际证据边界。
