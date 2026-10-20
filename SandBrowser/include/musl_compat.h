/* musl 的内部可见性/别名约定，不引入 Linux 系统调用后端。 */
#define hidden __attribute__((visibility("hidden")))
#define weak_alias(old, new) extern __typeof(old) new __attribute__((weak, alias(#old)));
