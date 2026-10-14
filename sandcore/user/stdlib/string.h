#ifndef SCCLIB_STRING_H
#define SCCLIB_STRING_H
#include "stddef.h"
void *memcpy(void *dest,const void *source,size_t count);
void *memmove(void *dest,const void *source,size_t count);
void *memset(void *dest,int value,size_t count);
int memcmp(const void *left,const void *right,size_t count);
void *memchr(const void *data,int value,size_t count);
size_t strlen(const char *text);
char *strcpy(char *dest,const char *source);
char *strncpy(char *dest,const char *source,size_t count);
char *strcat(char *dest,const char *source);
char *strncat(char *dest,const char *source,size_t count);
int strcmp(const char *left,const char *right);
int strncmp(const char *left,const char *right,size_t count);
char *strchr(const char *text,int value);
char *strrchr(const char *text,int value);
size_t strspn(const char *text,const char *accept);
size_t strcspn(const char *text,const char *reject);
char *strpbrk(const char *text,const char *accept);
char *strstr(const char *text,const char *needle);
char *strtok(char *text,const char *delimiters);
char *strerror(int error);
int strcoll(const char *left,const char *right);
size_t strxfrm(char *dest,const char *source,size_t count);
#endif

