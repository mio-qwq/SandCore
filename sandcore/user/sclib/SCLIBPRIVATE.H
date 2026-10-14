#ifndef SANDCORE_SCLIB_PRIVATE_H
#define SANDCORE_SCLIB_PRIVATE_H
#include "SCLIB.H"
#include "SC32API.H"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

/* 代码寿命只走公开ScCallbackPin；依赖SC32API ABI1.1/version2。
 * 新源码仍待统一构建，不借旧85入口证据称这个增量已运行。 */
enum {SL_ARRAY=1,SL_LIST,SL_HASH,SL_STRING,SL_BUFFER,SL_STREAM,SL_CONFIG,SL_ERROR,SL_LOG,SL_EVENTS,SL_MODEL,SL_CACHE};
typedef struct SlBase SlBase;
struct SlBase {
    ScU32 kind,references,depth,closing,mutation,cleared;
    SlBase *previous,*next;
    void (*dispose)(SlBase *,int);
};
static SlBase *sl_objects;
static ScU32 sl_finalizing,sl_stopping,sl_token;
static void *sl_object(ScU32 kind,ScU32 bytes,void (*dispose)(SlBase *,int));
static ScStatus sl_valid(const void *object,ScU32 kind);
static ScStatus sl_enter(void *object,ScU32 kind);
static void sl_leave(void *object);
static ScStatus sl_retain(void *object,ScU32 kind);
static void sl_release(void *object,ScU32 kind);
static ScStatus sl_input(const void *record,ScU32 bytes);
static ScStatus sl_output(const void *record,ScU32 bytes);
static ScStatus sl_mutation(SlBase *base,ScU32 generation);
static ScStatus sl_identifier(ScU32 *out);
static ScStatus sl_pin(const void *function);
static ScU32 sl_capacity(ScU32 current,ScU32 wanted,ScU32 width);
static int sl_alias(const void *pointer,ScU32 bytes,const void *base,ScU32 span);
static void sl_counter_add(ScWideCounter *counter,ScU32 amount);
static void sl_info(ScContainerInfo *out,ScU32 length,ScU32 capacity,ScU32 width,ScU32 generation,ScU32 flags);
static void sl_view(ScView *out,const void *data,ScU32 bytes,ScU32 generation);
static ScStatus sl_string_bytes(const char *text,ScU32 *bytes);
static ScU32 sl_hash_capacity(ScU32 current,ScU32 entries);
static ScStatus sl_value_clone(const ScValue *input,ScValue *out);
static void sl_value_free(ScValue *value);

struct ScArray {SlBase base;ScU32 count,capacity,width,generation,trusted_drop;unsigned char *data;ScDropProc drop;void *context;};
typedef struct SlListNode SlListNode;
struct SlListNode {SlListNode *previous,*next,*retired,*bucket;ScU32 token,alive,alignment[2];unsigned char data[1];};
struct ScList {SlBase base;SlListNode **buckets,*first,*last,*retired;ScU32 count,capacity,width,generation,walk;ScDropProc drop;void *context;};
typedef struct SlHashNode SlHashNode;
struct SlHashNode {SlHashNode *bucket,*previous,*next,*retired;ScU32 hash,token,key_bytes,alive,depth;unsigned char *key,*value;};
struct ScHash {SlBase base;SlHashNode **buckets,*first,*last,*retired;ScU32 count,capacity,width,generation,walk,trusted_drop;ScDropProc drop;void *context;};
struct ScString {SlBase base;unsigned char *data;ScU32 bytes,capacity,generation,borrowed;};
struct ScBuffer {SlBase base;unsigned char *data;ScU32 bytes,capacity,generation,borrowed;};

#endif
