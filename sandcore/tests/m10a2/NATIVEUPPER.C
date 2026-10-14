/* mio：只用 SCCC 已有整数/指针/数组/cdecl 子集；重定位交给新 SCL 输出。
 * 依赖只有清单声明的 BASE，不复制它的正文，也不带退出系统调用。 */
extern int base_value;
extern int base_add(int value);

int upper_value = 5;
int upper_state;
int upper_init_count;
int upper_fini_count;
int upper_bss[4];
const char upper_message[] = "SCL upper";
int *upper_base_pointer = &base_value;
int *upper_value_pointer = &upper_value;
const char *upper_message_pointer = upper_message;
int (*upper_add_pointer)(int) = base_add;

int upper_internal_add(int value)
{
    return value + *upper_value_pointer;
}

int upper_compute(int value)
{
    return base_add(value) + upper_internal_add(value) + *upper_base_pointer
        + upper_state + upper_bss[3] + (int)upper_message_pointer[0];
}

int upper_call_pointer(int value)
{
    return upper_add_pointer(value) + *upper_value_pointer;
}

int upper_init(void)
{
    upper_state = base_add(5);
    upper_bss[3] = base_value + 3;
    ++upper_init_count;
    return 0;
}

void upper_fini(void)
{
    upper_bss[3] = base_add(1);
    upper_state = -23;
    ++upper_fini_count;
}
