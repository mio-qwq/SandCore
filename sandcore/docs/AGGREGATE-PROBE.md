# SCUI 所需结构体调用的定向回归

当前状态：build11和platform-guest-04已完成真实构建及普通用户两方向运行，
每个方向55检查通过，栈差额0且owner页账恢复。完整报告及原生产物见
[第一轮证据](../evidence/m10a2-ui-platform/README.md)。此夹具由真实
ui-native-host-01 的 ui_data.c:1394 诊断触发，不是新增旧系统全量测试门槛。

AGGABI.H 定义8/12/16/32B结构体及8B联合体；Callee只做明确整数变换，
Probe独立比较原值与算术预期。覆盖按值隔离、相邻scalar参数、嵌套返回、
返回对象成员、32B数组结果、1/2/4B混合字段、联合体、反向函数指针回调、
条件结构体返回、最后named aggregate参数的va_start和重复调用后的scalar。
同一AggStackSample函数调用前后只比较局部地址转整数，不解引用已结束对象；
实际GCC机器码为SUB ESP16/LEA ESP+12/ADD ESP16/RET，没有realign掩蔽。
前后两次均用相同局部初始化表达式采样，避免混入外层check已压入的参数。

仅两个真实普通用户方向：

1. 执行的G1编译Probe，运行时调用GCC Callee，并由Callee回调G1函数。
2. GCC Probe运行时调用执行的G1编译Callee，并由Callee回调GCC函数。

每个方向要求实际 stdout `AGGABI PASS checks=55 failures=0`、退出0及完整
页/owner回收；没有新增其它排列组合。普通caller目录分别为
HOME/M10UI/AGG/NATIVECALL与NATIVELIB，避免同名生成库遮蔽SYS/LIB五个
待正式GUI执行的系统库。每份原生文件通过实际get提取与manifest合同校验，
报告aggregate_abi单列来源、compiler SHA与实际执行方向。

宿主真实SCCC适配器预检只编译这两份输入和完整SCUI/三个app，不运行i386，
不能冒充G1生成或客体执行。第一轮generated_library_execution可能为true
（定向Callee已实际运行），generated_system_libraries_execution仍为false
（新的SC32/SCUI系统库尚未安装）；GUI主控必须分别核这两个边界。

2026-10-15：根据实际SCUI原生语法缺口新增必要的交叉工具链ABI夹具；真实两方向通过，
保留guest03采样表达式位置不同的FAIL与修复，不扩其它排列组合。
