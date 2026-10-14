# M10a2 自动动态SCX检查点

本目录为2026-10-14的实际阶段证据，完整目标仍active。新平台的SC32API、SCCLIB、SCLIB、SCUI与桌面/组件/SDK统一验收继续；截图仍是已有Aurora桌面。

- `build.json`：冻结源码的WSL ELF定向构建，CORE完整SHA256为`7d45089e45303b0fa21cb9e2934c0add365d83b5ebf914d0bba655654e503dcb`。
- `host.json`：真实host/native编译后端与共同SCD/CRT检查；没有执行i386，旧未受影响项按摘要复用。
- `guest.json`：一个Windows双盘VM的30个定向步骤；宿主应用、客体SCCC0和实际G1编译动态应用并自动执行，依赖init共享context、两并发进程独立库数据、ROOT只读/#PF回调退出、页8872→8872、坏格式/版本/缺库/系统保护与旧SCX运行。
- `SCLDR-HOST.SCL`、`DYNAPP-HOST.SCX`：宿主GCC引导产物，实际在本轮客体执行。
- `DYNAPP-NATIVE.SCX`、`SCCC-G1.SCX`：本轮客体SCCC生成并实际执行；G0/G1应用字节相同。
- `SCLDR-NATIVE.SCL`：本轮客体生成且格式通过，**该生成文件尚未执行**，不能替代已安装宿主根的来源。
- `desktop.png`：真实HMP F1和1920×1080截图，已查看；不是新SCUI/桌面完成图。
- `first-fault-pause.json/png`：第一次测试未注册异常回调却等EXIT14，沿旧故障暂停合同超时；内核行为未改，修正私有测试库的回调后真实#PF退出通过。
- `cleanup.json`：首次失败两张无用生成盘清理269910016B，代码/日志/截图/原盘和成功当前盘均保留。

重现命令与目录见`docs/BUILD.md`。M10a1数据盘SHA256在前后均为`3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64`。完整源码来源、init/fini、借用根、ABI和实际限制见`M10A2-IMPLEMENTATION.md`、`SCLDR.md`和`SCX-DYNAMIC.md`。
