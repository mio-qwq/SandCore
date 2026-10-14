#ifndef SCCLIB_STDLIB_H
#define SCCLIB_STDLIB_H
#include "stddef.h"
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 32767
void *malloc(size_t bytes);
void *calloc(size_t count,size_t bytes);
void *realloc(void *memory,size_t bytes);
void free(void *memory);
int atoi(const char *text);
long atol(const char *text);
long strtol(const char *text,char **end,int base);
unsigned long strtoul(const char *text,char **end,int base);
int abs(int value);
long labs(long value);
void qsort(void *base,size_t count,size_t width,int (*compare)(const void *,const void *));
void *bsearch(const void *key,const void *base,size_t count,size_t width,int (*compare)(const void *,const void *));
int rand(void);
void srand(unsigned int seed);
char *getenv(const char *name);
int atexit(void (*callback)(void));
void exit(int status);
void _Exit(int status);
void abort(void);
#endif

