/* mio：这两个库只验证普通 cdecl 代码与动态数据；不嵌入系统调用桥，
 * 初始化计数存于 BSS，便于观察重复装载是否错误地再次初始化。 */
int base_value = 7;
int base_state;
int base_init_count;
int base_fini_count;
const char base_message[] = "SCL base";

int base_add(int value)
{
    return value + base_value;
}

int base_init(void)
{
    base_state = 11;
    ++base_init_count;
    return 0;
}

void base_fini(void)
{
    base_state = -11;
    ++base_fini_count;
}
