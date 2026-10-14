/* mio：普通动态应用只有公开函数/数据声明；自动CRT和ROOT运行时
 * 负责装载BASE/UPPER、调用main及清理，不含SCAPI或软中断。 */
extern int base_value;
extern int base_init_count;
extern int base_add(int value);
extern int upper_init_count;
extern int upper_compute(int value);
extern int upper_call_pointer(int value);
extern const char upper_message[];

int application_value = 5;
int application_bss[4];
int *application_bss_pointer = application_bss;
int *application_value_pointer = &application_value;
int *application_base_pointer = &base_value;
int (*application_add_pointer)(int) = base_add;
const char *application_message_pointer = upper_message;
const char application_label[] = "Dynamic SCX";
const char *application_label_pointer = application_label;

static int application_internal(int value)
{
    return value + *application_value_pointer;
}

int main(void)
{
    if(base_init_count != 1 || upper_init_count != 1) return 1;
    if(base_value != 7 || application_bss[0] || application_bss[3]) return 2;
    if(upper_compute(10) != 144 || upper_call_pointer(10) != 22) return 3;
    if(application_add_pointer(10) != 17 || *application_base_pointer != 7) return 4;
    if(application_message_pointer[0] != 'S' || application_label_pointer[0] != 'D') return 5;
    if(application_bss_pointer != application_bss || application_internal(10) != 15) return 6;
    *application_bss_pointer = 23;
    if(application_bss[0] != 23 || application_bss[3]) return 7;
    return 0;
}
