/* Copyright (c) 2026 mio. 单线程三环 C++ 运行时；失败明确终止当前 worker。 */
#include "sb.h"
#include <new>
#include <cstdlib>
#include <string>
#include <stdexcept>
#include <memory>
#include <typeinfo>

static void failure(const char *why) {
    sb_log("SandBrowser renderer: %s\n", why);
    ScProcessExit(124);
    for (;;) {
    }
}
void *operator new(std::size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p)
        failure("Out of memory");
    return p;
}
void *operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void *p) noexcept {
    free(p);
}
void operator delete[](void *p) noexcept {
    free(p);
}
void operator delete(void *p, std::size_t) noexcept {
    free(p);
}
void operator delete[](void *p, std::size_t) noexcept {
    free(p);
}
void *operator new(std::size_t n, const std::nothrow_t &) noexcept {
    return malloc(n ? n : 1);
}
void operator delete(void *p, const std::nothrow_t &) noexcept {
    free(p);
}
namespace std {
bool _Sp_make_shared_tag::_S_eq(const type_info &t) noexcept {
    return !strcmp(t.name(), "St19_Sp_make_shared_tag");
}
} // namespace std
extern "C" int __cxa_guard_acquire(long long *g) {
    unsigned char *p = (unsigned char *)g;
    if (p[0])
        return 0;
    if (p[1])
        failure("Recursive static initialization");
    p[1] = 1;
    return 1;
}
extern "C" void __cxa_guard_release(long long *g) {
    ((unsigned char *)g)[0] = 1;
    ((unsigned char *)g)[1] = 0;
}
extern "C" void __cxa_guard_abort(long long *g) {
    ((unsigned char *)g)[1] = 0;
}
extern "C" void __cxa_pure_virtual() {
    failure("Invalid virtual call");
}
namespace std {
void __throw_bad_alloc() {
    failure("Out of memory");
}
void __throw_bad_array_new_length() {
    failure("Array allocation overflow");
}
void __throw_length_error(const char *s) {
    failure(s);
}
void __throw_out_of_range(const char *s) {
    failure(s);
}
void __throw_out_of_range_fmt(const char *, ...) {
    failure("Container index out of range");
}
void __throw_logic_error(const char *s) {
    failure(s);
}
void __throw_runtime_error(const char *s) {
    failure(s);
}
void __throw_bad_function_call() {
    failure("Empty callback");
}
void __throw_bad_cast() {
    failure("Invalid cast");
}
} // namespace std
