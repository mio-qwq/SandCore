#include "../SCIO.H"
#include "../SCTEXT.H"
int main(void)
{
    u32 microseconds;if(cli_parse()!=1 || text_number(cli_argv[0],&microseconds)<0)return 2;
    /* PIT只有10ms量化，不谎报微秒精度；非零向上取整，期间让出CPU。 */
    u32 ticks=microseconds/10000+(microseconds%10000!=0),begin=(u32)sc_tick();
    while((u32)sc_tick()-begin<ticks)sc_yield();return 0;
}
