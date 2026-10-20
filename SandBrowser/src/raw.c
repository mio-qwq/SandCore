/* Copyright (c) 2026 mio. 旧网络 ABI 只在私有适配层出现。 */
#include "SCAPI.H"
int sb_raw(int n, int a, int b, int c, int d, int e, int f) {
    return sc_call(n, a, b, c, d, e, f);
}
