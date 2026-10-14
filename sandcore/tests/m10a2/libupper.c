/* mio：保留直接调用、函数指针、外部数据指针和内部只读指针，
 * 让 ELF 输入实际含外部 ABS32/PC32 与内部 ABS32 重定位。
 * 内部函数单独成节，避免汇编器提前消掉所有内部 PC32 记录。 */
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

__attribute__((section(".text.upper_internal"), noinline))
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
    /* 结束回调仍调用依赖；反向清理若先卸载 BASE 会在这里暴露。 */
    upper_bss[3] = base_add(1);
    upper_state = -23;
    ++upper_fini_count;
}
