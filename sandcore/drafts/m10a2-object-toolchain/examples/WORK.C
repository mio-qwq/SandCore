/* 与MAIN.C同名的static拥有另一份存储，不逃逸为公共符号。 */
static int private_value=5;
int ProjectValue=7;
int ProjectStep(int value)
{
    return value+private_value+ProjectValue;
}
