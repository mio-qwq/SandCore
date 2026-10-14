#ifndef SCCLIB_STDIO_H
#define SCCLIB_STDIO_H
#include "stddef.h"
#include "stdarg.h"
typedef struct __scc_file FILE;
/* 记录为实现私有cookie，不能当long偏移或Sc32句柄使用。 */
typedef struct { unsigned int position,valid; } fpos_t;
#define EOF (-1)
#define BUFSIZ 4096
/* 最低保证不是句柄槽上限，真正FILE沿动态链分配。 */
#define FOPEN_MAX 8
#define FILENAME_MAX 128
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
FILE *__scc_standard(unsigned int index);
#define stdin (__scc_standard(0))
#define stdout (__scc_standard(1))
#define stderr (__scc_standard(2))
FILE *fopen(const char *path,const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);
size_t fread(void *data,size_t width,size_t count,FILE *stream);
size_t fwrite(const void *data,size_t width,size_t count,FILE *stream);
int fseek(FILE *stream,long offset,int origin);
long ftell(FILE *stream);
void rewind(FILE *stream);
int fgetpos(FILE *stream,fpos_t *position);
int fsetpos(FILE *stream,const fpos_t *position);
int fgetc(FILE *stream);
int getc(FILE *stream);
int getchar(void);
int fputc(int value,FILE *stream);
int putc(int value,FILE *stream);
int putchar(int value);
char *fgets(char *data,int capacity,FILE *stream);
int fputs(const char *text,FILE *stream);
int puts(const char *text);
int ungetc(int value,FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
void setbuf(FILE *stream,char *buffer);
int setvbuf(FILE *stream,char *buffer,int mode,size_t capacity);
void perror(const char *prefix);
int printf(const char *format,...);
int fprintf(FILE *stream,const char *format,...);
int sprintf(char *data,const char *format,...);
int snprintf(char *data,size_t capacity,const char *format,...);
int vprintf(const char *format,va_list arguments);
int vfprintf(FILE *stream,const char *format,va_list arguments);
int vsprintf(char *data,const char *format,va_list arguments);
int vsnprintf(char *data,size_t capacity,const char *format,va_list arguments);
int remove(const char *path);
int rename(const char *before,const char *after);
#endif

