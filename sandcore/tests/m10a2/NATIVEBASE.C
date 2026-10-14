/* mio：由客体 SCCC 生成的 BASE，私有对象不能被清单错误导出。 */
int base_value = 7;
int base_state;
int base_init_count;
int base_fini_count;
const char base_message[] = "SCL base";
static int base_private = 2;

int base_add(int value)
{
    return value + base_value + base_private - 2;
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
