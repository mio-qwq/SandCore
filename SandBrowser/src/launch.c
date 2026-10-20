/* Copyright (c) 2026 mio. SCX 只作稳定入口，正文按 SCL 装载。 */
#ifndef SB_ENTRY
#define SB_ENTRY SbBrowserMain
#endif
extern int SB_ENTRY(void);
int main(void) {
    return SB_ENTRY();
}
