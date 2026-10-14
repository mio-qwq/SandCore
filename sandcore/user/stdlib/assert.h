/* NDEBUG可在两次包含之间改变，因此宏选择不能锁进整头guard。 */
#ifndef SCCLIB_ASSERT_DECLARED
#define SCCLIB_ASSERT_DECLARED 1
void __scc_assert_fail(const char *expression,const char *file,int line);
#endif
#undef assert
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression)?(void)0:__scc_assert_fail(#expression,__FILE__,__LINE__))
#endif

