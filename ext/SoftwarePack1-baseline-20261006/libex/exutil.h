/* =====================================================================
 * exutil.h —— libex 基础工具库（表达式/CSV/配置/UTF-8/整数数学/格式化）
 * SandCore 扩展生态共享代码。唯一内核 ABI 仍是 SCAPI.H；本库只做
 * 纯用户态计算与文件，不新增系统调用、不触碰浮点与 libc。
 * 全部函数带 ex_ 前缀；详见 ext/README.md。未实机验证。
 * ===================================================================== */
#ifndef EXUTIL_H
#define EXUTIL_H
#include "SCAPI.H"

/* ---------- 整数数学 ----------
 * 全整数实现：开方用逐位法（不构造中间乘积溢出），LCM 先除后乘
 * 防止 u32 回绕；随机数用 xorshift32，种子来自 PIT tick，调用方
 * 可以用时间/事件再扰动。 */
int ex_abs(int v);
int ex_min(int a,int b);
int ex_max(int a,int b);
int ex_clamp(int v,int lo,int hi);
u32 ex_sqrt_u32(u32 n);
u32 ex_gcd_u32(u32 a,u32 b);
u32 ex_lcm_u32(u32 a,u32 b);          /* 任一为 0 返回 0 */
u32 ex_rand_next(void);               /* 0..0xFFFFFFFF */
int ex_rand_range(int lo,int hi);     /* 含两端 */
void ex_rand_seed(u32 seed);

/* ---------- 文本与数值格式化 ----------
 * 输出缓冲必须由调用方给足容量；全部 NUL 结尾。 */
int ex_starts(const char *s,const char *prefix);
int ex_ends(const char *s,const char *suffix);
char ex_upper(char c);
void ex_dec(char *out,int capacity,int value);         /* 有符号十进制 */
void ex_udec(char *out,int capacity,u32 value);        /* 无符号十进制 */
void ex_hex(char *out,int width,u32 value);            /* 固定 width 位大写 */
void ex_bin(char *out,int capacity,u32 value,int bits);/* bits<=32 固定位 */

/* 解析：成功 0，失败 -1。ex_parse_int 接受可选 +/- 前缀与 0x/0b/0o
 * 基数前缀；溢出/空串/尾随垃圾都拒绝，绝不静默截断。 */
int ex_parse_int(const char *s,int *out);
int ex_parse_u32_hex(const char *s,u32 *out);          /* 严格 1..8 个 hex 位 */

/* ---------- UTF-8 ----------
 * ex_utf8_next 解码一个标量：返回消耗字节数，码点写入 *cp；
 * 非法序列消耗 1 字节并返回 0xFFFD 码点，保证前进不卡死。
 * ex_utf8_width 按凤凰字体估算显示宽（ASCII 8，其余 16）。 */
int ex_utf8_next(const char *s,u32 *cp);
int ex_utf8_width(const char *s);

/* ---------- 配置文件（key=value） ----------
 * 格式：# 注释、空行、key=value；key 为 ASCII 标识符，值不含 LF。
 * 解析纯内存，不检查重复；写入一次性生成完整正文，调用方 sc_write。 */
int ex_cfg_get(const char *body,const char *key,char *out,int capacity);
/* options 为 "k1=v1\nk2=v2" 正文；写入时带文件头注释。返回正文字节数。 */
int ex_cfg_build(char *out,int capacity,const char *app,const char *version,
                 const char *body);

/* ---------- CSV ----------
 * 解析单行到字段指针数组：支持双引号包裹与 "" 转义；字段内容
 * 原地共享行缓冲（引号被折掉），调用方在使用期内不得释放行。
 * 返回字段数；fields 满时剩余内容并入最后一个字段。
 * ex_csv_join 反向：按 RFC4180 风格给含 逗号/引号/CR/LF 的字段加引号。 */
int ex_csv_split(char *line,char **fields,int max_fields);
int ex_csv_join(char *out,int capacity,const char **fields,int count);

/* ---------- 表达式引擎（32 位整数） ----------
 * 语法：十进制/0x十六/0b二/0o八进制字面量；'c' 字符字面量；
 * 变量名回调；函数调用 name(a,b,...)；运算符优先级从低到高：
 * ?: || && | ^ & == != < <= > >= << >> + - * / % 一元 ! ~ - + ()。
 * 语义与主项目 SCARITH 一致：加减乘按二补码回绕，除零置 error，
 * 逻辑短路不执行未选分支。回调返回 0 且 *present=0 表示变量未知，
 * 整体置 error，不能把未知变量悄悄当 0。
 * ex_eval 成功 0 并写 *out；失败 -1 且 err_pos 为出错字节偏移。 */
typedef int (*ex_var_fn)(const char *name,int *out);
typedef int (*ex_call_fn)(const char *name,const int *args,int argc,int *out);
int ex_eval(const char *text,ex_var_fn vars,ex_call_fn calls,int *out,int *err_pos);

/* ---------- 行协议目录读取 ----------
 * SC_DIR 输出 "D/F 名字 大小\n"；这里解析成并行数组，容量不足
 * 返回 -1 且不写半条。用于安装器卸载与文件选择扩展。 */
int ex_dir_parse(const char *list,char names[][64],int *kinds,int max);

#endif /* EXUTIL_H */
