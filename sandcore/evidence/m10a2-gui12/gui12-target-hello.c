#include "SCAPI.H"

int main(void)
{
    int w=sc_open("Hello / mio",306,164);
    sc_text(w,8,32,"Made inside SandCore",PAL_TITLE);
    wait_escape();
    return 0;
}
