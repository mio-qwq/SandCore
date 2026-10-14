#ifndef SCCLIB_ERRNO_H
#define SCCLIB_ERRNO_H
/* 这些正值属于SCCLIB v1；SC32的负status/domain/raw仍单独查询。 */
#define EPERM 1
#define ENOENT 2
#define EIO 5
#define EBADF 9
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EBUSY 16
#define EEXIST 17
#define EINVAL 22
#define ENOSPC 28
#define ESPIPE 29
#define EPIPE 32
#define EDOM 33
#define ERANGE 34
#define ENOSYS 38
#define EOVERFLOW 75
#define EILSEQ 84
#define ESTALE 116
#define ECANCELED 125
int *__scc_errno_location(void);
#define errno (*__scc_errno_location())
#endif

