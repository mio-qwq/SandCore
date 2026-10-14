#include "SCCLIBPRIVATE.H"
/* 动态库RW映像属于本进程；不在内核或每个头文件另放errno副本。 */
static int __scc_errno_value;
int *__scc_errno_location(void) {
    return &__scc_errno_value;
}
static int __scc_from_status(ScStatus status)
{
    int error=EIO;
    if(status==SC32_E_INVALID)error=EINVAL;
    else if(status==SC32_E_VERSION || status==SC32_E_NOT_SUPPORTED)error=ENOSYS;
    else if(status==SC32_E_ACCESS)error=EACCES;
    else if(status==SC32_E_NOT_FOUND)error=ENOENT;
    else if(status==SC32_E_EXISTS)error=EEXIST;
    else if(status==SC32_E_MEMORY)error=ENOMEM;
    else if(status==SC32_E_NO_SPACE)error=ENOSPC;
    else if(status==SC32_E_STALE)error=ESTALE;
    else if(status==SC32_E_AGAIN)error=EAGAIN;
    else if(status==SC32_E_BROKEN_PIPE)error=EPIPE;
    else if(status==SC32_E_BUSY)error=EBUSY;
    else if(status==SC32_E_RANGE)error=EOVERFLOW;
    else if(status==SC32_E_CANCELLED)error=ECANCELED;
    errno=error;
    return error;
}
char *strerror(int error)
{
    switch(error){
        case 0:return "No error";
        case EPERM:return "Operation not permitted";
        case ENOENT:return "No such file or directory";
        case EIO:return "Input/output error";
        case EBADF:return "Bad stream";
        case EAGAIN:return "Operation would block";
        case ENOMEM:return "Out of memory";
        case EACCES:return "Permission denied";
        case EBUSY:return "Resource busy";
        case EEXIST:return "File exists";
        case EINVAL:return "Invalid argument";
        case ENOSPC:return "No space left";
        case ESPIPE:return "Stream is not seekable";
        case EPIPE:return "Broken pipe";
        case EDOM:return "Domain error";
        case ERANGE:return "Result out of range";
        case ENOSYS:return "Unsupported operation";
        case EOVERFLOW:return "Value cannot be represented";
        case EILSEQ:return "Invalid character sequence";
        case ESTALE:return "Stale object";
        case ECANCELED:return "Operation cancelled";
        default:return "Unknown error";
    }
}
