#ifndef SANDCORE_SCLDR_H
#define SANDCORE_SCLDR_H

/* M10a2 的用户态引导接口。平台回调只用于引导/验证适配；普通应用
 * 后续通过 SC32API 使用它们，不复制 syscall 桥或内核结构。 */
#include "../SCTYPES.H"
typedef ScU32 ScModule;
typedef struct ScRuntime ScRuntime;

#define SC_SCL_OK 0
#define SC_SCL_RESIDENT 1
#define SC_SCL_FORMAT -1
#define SC_SCL_RANGE -2
#define SC_SCL_SYMBOL -3
#define SC_SCL_VERSION -4
#define SC_SCL_NOT_FOUND -5
#define SC_SCL_MEMORY -6
#define SC_SCL_CYCLE -7
#define SC_SCL_INIT -8
#define SC_SCL_HANDLE -9
#define SC_SCL_BUSY -10
#define SC_SCL_IO -11

typedef struct {
    ScU32 size;
    void *context;
    void *(*allocate)(void *context, ScU32 bytes);
    void (*release)(void *context, void *memory);
    int (*read_file)(void *context, const char *name, void **bytes, ScU32 *size);
    void (*release_file)(void *context, void *bytes);
    void *(*map_image)(void *context, ScU32 bytes);
    void (*unmap_image)(void *context, void *base);
    int (*protect_image)(void *context, void *base, ScU32 bytes, ScU32 access);
    int (*invoke)(void *context, void *entry, int finalizing);
} ScRuntimePlatform;

typedef struct {
    ScU32 size, version;
    ScU32 modules, image_bytes, metadata_bytes, pinned_modules;
    ScU32 loads, reuses, relocations;
} ScRuntimeStats;

ScRuntime *ScRuntimeCreate(const ScRuntimePlatform *platform);
int ScRuntimeOpen(ScRuntime *runtime, const char *name, ScU32 abi_major,
                  ScU32 min_minor, ScU32 min_version, ScModule *module);
int ScRuntimeClose(ScRuntime *runtime, ScModule module);
/* 符号地址会逃逸到应用/库的任意位置；第一次查询即固定该模块至
 * 进程结束，Close 只释放显式引用并返回 RESIDENT。不能猜调用栈
 * 是否仍持有地址后强制卸载。依赖边同时保护被导入模块。 */
int ScRuntimeSymbol(ScRuntime *runtime, ScModule module, const char *name,
                    ScU32 kind, void **address);
int ScRuntimeResolve(ScRuntime *runtime, ScModule module, const char *name,
                     ScU32 kind, ScU32 minimum_size, void **address);
/* 引导根由内核只做页装载，真正依赖解析仍在三环。本接口将已经验证
 * 的根元数据借入同一模块图；根代码正在执行，Destroy不自行卸载它。 */
int ScRuntimeAdoptBootstrap(ScRuntime *runtime, void *image,
                           const void *metadata, ScU32 metadata_size);
ScRuntime *ScApplicationRuntime(void);
/* ABI1.1 / library_version2：正常终止只由自动应用ROOT协调。
 * prepare仅在SCCLIB真实init期间登记，成功init后提交；callback pin
 * 接受主映像或READY库的内部函数地址，失败不留下新resident引用。 */
int ScApplicationRegisterPrepare(void (*prepare)(void));
int ScApplicationPinCallback(void (*callback)(void));
/* 实际永不返回；脱离应用/库帧后prepare→fini→CRT原EXIT。
 * 初始化尚未提交、递归终止或不合法控制点走明确异常失败。 */
void ScApplicationExit(int status);
int ScRuntimeQuery(ScRuntime *runtime, ScRuntimeStats *stats);
/* 仅在 main 已返回、没有任何库调用栈/回调时关闭运行时。异常退出
 * 由内核直接回收私有页，不承诺执行可能已经损坏的用户态析构。 */
int ScRuntimeDestroy(ScRuntime *runtime);
/* package_root 必须为绝对路径并在运行时存活期间保持有效；系统库
 * 固定从 /SYS/LIB 获取。第三方库从显式包目录获取，没有 CWD/PATH
 * 隐式搜索。NULL 表示只使用系统目录。 */
int ScRuntimeDefaultPlatform(ScRuntimePlatform *platform, const char *package_root);

#endif
