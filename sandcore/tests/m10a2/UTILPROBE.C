/* 正式动态应用，只调用公开库；少量完整工作流验证真正输出/归属。
 * 任何失败都有唯一marker，成功main返回后由ROOT统一fini/页回收。 */
#include "SCLIB.H"
#include "SC32API.H"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static unsigned int checks,failures;
static void record(void *out,unsigned int bytes)
{
    memset(out,0,bytes);
    ((ScU32 *)out)[0]=bytes;
    ((ScU32 *)out)[1]=1;
}
static void check(const char *name,int value)
{
    checks++;
    if(!value){failures++;printf("UTIL FAIL %s\n",name);}
}
static int status(const char *name,ScStatus actual,ScStatus expected)
{
    checks++;
    if(actual==expected)return 1;
    failures++;printf("UTIL FAIL %s got=%d expected=%d\n",name,actual,expected);return 0;
}
static int text_equal(ScString *string,const char *expected)
{
    const char *text=NULL;
    return ScStringCStr(string,&text)==0 && !strcmp(text,expected);
}
typedef struct {ScU32 calls,sum;} DropState;
static void drop_number(void *item,void *context)
{
    DropState *state=context;ScU32 value=0;
    memcpy(&value,item,sizeof(value));state->calls++;state->sum+=value;
}
typedef struct {
    ScList *list;ScU32 first,future,added,calls;DropState *drops;
} ListState;
static ScStatus visit_list(const ScListEntry *entry,void *context)
{
    ListState *state=context;state->calls++;
    if(entry->token==state->first){
        ScU32 added=40;
        ScStatus result=ScListRemove(state->list,entry->token);
        if(result)return result;
        result=ScListInsert(state->list,0,&added,&state->added);
        if(result)return result;
    }else{
        ScStatus result=ScListRemove(state->list,state->future);
        if(result)return result;
    }
    /* 旧entry/next仍由visit保护，drop不能提前使callback的数据悬空。 */
    if(state->drops->calls)return SC_UTIL_E_INVALID;
    return 0;
}
typedef struct {ScHash *hash;ScU32 calls,sum,busy;} HashState;
static ScStatus visit_hash(const ScHashEntry *entry,void *context)
{
    HashState *state=context;ScU32 value;
    memcpy(&value,entry->value,sizeof(value));state->sum+=value;state->calls++;
    value++;
    if(ScHashSet(state->hash,entry->key,entry->key_bytes,&value,SC_HASH_REPLACE_ONLY)==SC_UTIL_E_BUSY)state->busy++;
    else return SC_UTIL_E_INVALID;
    return 0;
}
static void containers(void)
{
    ScArray *array=NULL;ScList *list=NULL;ScHash *hash=NULL;
    DropState array_drop={0,0},list_drop={0,0},hash_drop={0,0};
    ScU32 numbers[3]={10,20,30},value=0;
    ScArrayDesc ad;record(&ad,sizeof(ad));ad.item_bytes=sizeof(value);ad.initial_capacity=2;
    ad.drop=drop_number;ad.context=&array_drop;
    if(!status("array-create",ScArrayCreate(&ad,&array),0))goto done;
    if(!status("array-insert",ScArrayInsert(array,0,numbers,3),0))goto done;
    ScView old;record(&old,sizeof(old));
    if(!status("array-view",ScArrayView(array,&old),0))goto done;
    check("array-bytes",old.bytes==12 && !memcmp(old.data,numbers,12));
    if(!status("array-alias-insert",ScArrayInsert(array,1,(const unsigned char *)old.data+4,1),0))goto done;
    ScContainerInfo info;record(&info,sizeof(info));
    status("array-info",ScArrayQuery(array,&info),0);
    check("array-old-view-stale",info.generation!=old.generation && info.length==4);
    status("array-get-alias",ScArrayGet(array,2,&value),0);check("array-alias-value",value==20);
    status("array-erase",ScArrayErase(array,2,1),0);
    ScArrayRelease(array);array=NULL;
    check("array-drops",array_drop.calls==4 && array_drop.sum==80);

    ScListDesc ld;record(&ld,sizeof(ld));ld.item_bytes=4;ld.drop=drop_number;ld.context=&list_drop;
    if(!status("list-create",ScListCreate(&ld,&list),0))goto done;
    ScU32 first=0,second=0,third=0;
    status("list-first",ScListInsert(list,0,numbers,&first),0);
    status("list-second",ScListInsert(list,0,numbers+1,&second),0);
    ScIterator iterator;record(&iterator,sizeof(iterator));
    status("list-iterator",ScListIterate(list,&iterator),0);
    status("list-third",ScListInsert(list,0,numbers+2,&third),0);
    ScListEntry entry;record(&entry,sizeof(entry));entry.token=0x1234u;
    status("list-stale-next",ScListNext(&iterator,&entry),SC_UTIL_E_STALE);
    check("list-stale-output",entry.token==0x1234u);
    ListState ls={list,first,third,0,0,&list_drop};
    status("list-reentrant-visit",ScListVisit(list,visit_list,&ls),0);
    check("list-watermark",ls.calls==2 && ls.added!=0 && list_drop.calls==2 && list_drop.sum==40);
    record(&iterator,sizeof(iterator));status("list-new-iterator",ScListIterate(list,&iterator),0);
    record(&entry,sizeof(entry));if(!status("list-remains-second",ScListNext(&iterator,&entry),0))goto done;
    memcpy(&value,entry.data,4);check("list-second-value",entry.token==second && value==20);
    record(&entry,sizeof(entry));if(!status("list-remains-added",ScListNext(&iterator,&entry),0))goto done;
    memcpy(&value,entry.data,4);check("list-added-value",entry.token==ls.added && value==40);
    record(&entry,sizeof(entry));status("list-end",ScListNext(&iterator,&entry),SC_UTIL_END);
    ScListRelease(list);list=NULL;
    check("list-drops",list_drop.calls==4 && list_drop.sum==100);

    ScHashDesc hd;record(&hd,sizeof(hd));hd.value_bytes=4;hd.initial_capacity=2;
    hd.drop=drop_number;hd.context=&hash_drop;
    if(!status("hash-create",ScHashCreate(&hd,&hash),0))goto done;
    unsigned char key[2]={0,255};const unsigned char lookup[2]={0,255};value=7;
    status("hash-binary-key",ScHashSet(hash,key,2,&value,SC_HASH_INSERT_ONLY),0);
    key[0]=1;key[1]=2;value=9;
    status("hash-text-key",ScHashSet(hash,"key",3,&value,SC_HASH_INSERT_ONLY),0);
    record(&iterator,sizeof(iterator));status("hash-iterator",ScHashIterate(hash,&iterator),0);
    value=14;status("hash-replace",ScHashSet(hash,lookup,2,&value,SC_HASH_REPLACE_ONLY),0);
    ScHashEntry he;record(&he,sizeof(he));he.token=0x5678u;
    status("hash-stale-next",ScHashNext(&iterator,&he),SC_UTIL_E_STALE);
    check("hash-stale-output",he.token==0x5678u);
    record(&he,sizeof(he));if(!status("hash-owned-key",ScHashGet(hash,lookup,2,&he),0))goto done;
    memcpy(&value,he.value,4);check("hash-owned-value",value==14 && he.key_bytes==2 && !memcmp(he.key,lookup,2));
    HashState hs={hash,0,0,0};status("hash-held-value-visit",ScHashVisit(hash,visit_hash,&hs),0);
    check("hash-visit-values",hs.calls==2 && hs.sum==23 && hs.busy==2);
    ScHashRelease(hash);hash=NULL;
    check("hash-drops",hash_drop.calls==3 && hash_drop.sum==30);
done:
    if(hash)ScHashRelease(hash);
    if(list)ScListRelease(list);
    if(array)ScArrayRelease(array);
    printf("UTIL containers drops=%u/%u/%u\n",array_drop.calls,list_drop.calls,hash_drop.calls);
}
static const unsigned char smile[4]={0xF0u,0x9Fu,0x99u,0x82u};
static const unsigned char bad_utf8[4]={0xF0u,0x80u,0x80u,0x80u};
static void strings_paths_config(void)
{
    const unsigned char borrowed[6]={'A',0xF0u,0x9Fu,0x99u,0x82u,'Z'};
    const unsigned char expected[8]={'A',0xF0u,0x9Fu,0x99u,0x82u,'Z',':','A'};
    ScString *string=NULL,*path=NULL,*relative=NULL,*joined=NULL,*serialized=NULL,*again=NULL;
    ScConfig *config=NULL,*copy=NULL;
    if(!status("string-borrow",ScStringBorrow(borrowed,sizeof(borrowed),&string),0))goto done;
    ScView view;record(&view,sizeof(view));status("string-borrow-view",ScStringView(string,&view),0);
    check("string-borrow-address",view.data==borrowed && view.bytes==6);
    ScU32 old_generation=view.generation;
    status("string-cow",ScStringAppend(string,":",1),0);
    record(&view,sizeof(view));status("string-owned-view",ScStringView(string,&view),0);
    check("string-cow-source",borrowed[5]=='Z' && view.generation!=old_generation && view.data!=borrowed);
    old_generation=view.generation;
    status("string-alias-append",ScStringAppend(string,view.data,1),0);
    record(&view,sizeof(view));status("string-final-view",ScStringView(string,&view),0);
    check("string-final-bytes",view.generation!=old_generation && view.bytes==8 && !memcmp(view.data,expected,8));
    const char *text=NULL;if(!status("string-cstr",ScStringCStr(string,&text),0))goto done;
    check("string-cstr-nul",strlen(text)==8 && text[8]==0);
    ScU32 count=0,offset=1,scalar=0;
    status("utf8-count",ScUtf8Count(text,8,&count),0);check("utf8-scalars",count==5);
    status("utf8-four-byte",ScUtf8Decode(text,8,&offset,&scalar),0);
    check("utf8-four-value",scalar==0x1F642u && offset==5);
    status("utf8-previous",ScUtf8Previous(text,8,&offset,&scalar),0);
    check("utf8-previous-value",scalar==0x1F642u && offset==1);
    offset=0;scalar=0xabcdefu;
    status("utf8-reject-overlong",ScUtf8Decode(bad_utf8,4,&offset,&scalar),SC_UTIL_E_UTF8);
    check("utf8-failure-preserves",offset==0 && scalar==0xabcdefu);
    unsigned char encoded[4]={0,0,0,0};ScU32 written=0;
    status("utf8-encode",ScUtf8Encode(0x1F642u,encoded,4,&written),0);
    check("utf8-encoded-bytes",written==4 && !memcmp(encoded,smile,4));
    check("unicode-basic",!ScUnicodeIsScalar(0xD800u) && ScUnicodeIsNoncharacter(0xFDD0u) && ScUnicodeIsWhitespace(0x2003u));

    ScU32 flags=SC_PATH_REQUIRE_ABSOLUTE|SC_PATH_REJECT_ABOVE_ROOT;
    status("path-normalize",ScPathNormalize("../DATA/./note.ini","/HOME/UTIL/CFG",flags,&path),0);
    check("path-normalized",text_equal(path,"/HOME/UTIL/DATA/note.ini"));
    status("path-relative",ScPathRelative("/HOME/UTIL/CFG","/HOME/UTIL/DATA/note.ini",flags,&relative),0);
    check("path-relative-text",text_equal(relative,"../DATA/note.ini"));
    status("path-join",ScPathJoin("/HOME/UTIL","DATA/../DATA/note.ini",flags,&joined),0);
    check("path-join-text",text_equal(joined,"/HOME/UTIL/DATA/note.ini"));
    ScPathParts parts;record(&parts,sizeof(parts));
    status("path-split",ScPathSplit("/HOME/UTIL/DATA/note.ini",&parts),0);
    check("path-parts",parts.rooted && parts.name_bytes==8 && parts.extension_bytes==4);
    ScString *same=path;
    status("path-reject-root-escape",ScPathNormalize("/../../SYS",NULL,flags,&path),SC_UTIL_E_RANGE);
    check("path-failure-owner",path==same && text_equal(path,"/HOME/UTIL/DATA/note.ini"));

    const char *ini="[ui]\nwidth=640u\nvisible=true\nlabel=\"A\\uD83D\\uDE42\"\nseed=counter(00000001,00000002)\nblob=hex\"00ff\"\n";
    if(!status("config-create",ScConfigCreate(&config),0))goto done;
    ScParseIssue issue;record(&issue,sizeof(issue));
    if(!status("config-parse",ScConfigParse(config,ini,(ScU32)strlen(ini),0,&issue),0))goto done;
    ScValue value;record(&value,sizeof(value));status("config-width",ScConfigGet(config,"ui","width",&value),0);
    check("config-width-value",value.type==SC_VALUE_U32 && value.lo==640);
    record(&value,sizeof(value));status("config-label",ScConfigGet(config,"ui","label",&value),0);
    check("config-label-value",value.type==SC_VALUE_UTF8 && value.bytes==5 && !memcmp(value.data,expected,5));
    record(&value,sizeof(value));status("config-counter",ScConfigGet(config,"ui","seed",&value),0);
    check("config-counter-value",value.type==SC_VALUE_COUNTER && value.lo==1 && value.hi==2);
    record(&value,sizeof(value));status("config-bytes",ScConfigGet(config,"ui","blob",&value),0);
    const unsigned char binary[2]={0,255};check("config-bytes-value",value.type==SC_VALUE_BYTES && value.bytes==2 && !memcmp(value.data,binary,2));
    status("config-serialize",ScConfigSerialize(config,SC_CONFIG_SORT_KEYS,&serialized),0);
    ScContainerInfo before;record(&before,sizeof(before));status("config-before",ScConfigQuery(config,&before),0);
    const char *broken="[ui]\nwidth=1u\nbroken=\"unterminated\n";
    record(&issue,sizeof(issue));status("config-failed-candidate",ScConfigParse(config,broken,(ScU32)strlen(broken),0,&issue),SC_UTIL_E_SYNTAX);
    check("config-issue",issue.code==SC_UTIL_E_SYNTAX && issue.line==3);
    ScContainerInfo after;record(&after,sizeof(after));status("config-after",ScConfigQuery(config,&after),0);
    check("config-failure-generation",before.generation==after.generation && before.length==after.length);
    record(&value,sizeof(value));status("config-original-value",ScConfigGet(config,"ui","width",&value),0);
    check("config-original-width",value.lo==640 && value.type==SC_VALUE_U32);
    if(!status("config-copy-create",ScConfigCreate(&copy),0))goto done;
    record(&view,sizeof(view));status("config-serialized-view",ScStringView(serialized,&view),0);
    record(&issue,sizeof(issue));status("config-roundtrip-parse",ScConfigParse(copy,view.data,view.bytes,0,&issue),0);
    status("config-roundtrip-serialize",ScConfigSerialize(copy,SC_CONFIG_SORT_KEYS,&again),0);
    ScView roundtrip;record(&roundtrip,sizeof(roundtrip));status("config-roundtrip-view",ScStringView(again,&roundtrip),0);
    check("config-canonical-roundtrip",view.bytes==roundtrip.bytes && !memcmp(view.data,roundtrip.data,view.bytes));
done:
    if(again)ScStringRelease(again);
    if(serialized)ScStringRelease(serialized);
    if(copy)ScConfigRelease(copy);
    if(config)ScConfigRelease(config);
    if(joined)ScStringRelease(joined);
    if(relative)ScStringRelease(relative);
    if(path)ScStringRelease(path);
    if(string)ScStringRelease(string);
    puts("UTIL text-path-config finished");
}
static void buffers_binary_file(void)
{
    const unsigned char borrowed[4]={0,1,2,255},patch[2]={170,187},suffix[3]={'A',0,'Z'};
    const unsigned char expected_gap[9]={0,170,187,255,0,0,'A',0,'Z'};
    ScBuffer *buffer=NULL,*binary=NULL;ScStream *stream=NULL,*file_stream=NULL;FILE *file=NULL;
    if(!status("buffer-borrow",ScBufferBorrow(borrowed,4,&buffer),0))goto done;
    ScView view;record(&view,sizeof(view));status("buffer-old-view",ScBufferView(buffer,&view),0);
    ScU32 generation=view.generation;check("buffer-borrow-address",view.data==borrowed);
    status("buffer-cow-write",ScBufferWrite(buffer,1,patch,2),0);
    record(&view,sizeof(view));status("buffer-cow-view",ScBufferView(buffer,&view),0);
    check("buffer-cow-value",view.generation!=generation && borrowed[1]==1 && ((const unsigned char *)view.data)[1]==170);
    if(!status("stream-from-buffer",ScStreamFromBuffer(buffer,SC_STREAM_READ|SC_STREAM_WRITE|SC_STREAM_SEEK,&stream),0))goto done;
    ScU32 position=0,done_bytes=0;
    status("stream-hole-seek",ScStreamSeek(stream,6,SEEK_SET,&position),0);check("stream-hole-position",position==6);
    status("stream-hole-write",ScStreamWrite(stream,suffix,3,&done_bytes),0);check("stream-write-count",done_bytes==3);
    status("stream-rewind",ScStreamSeek(stream,0,SEEK_SET,&position),0);
    unsigned char readback[32];memset(readback,0xcc,sizeof(readback));
    status("stream-buffer-read",ScStreamRead(stream,readback,9,&done_bytes),0);
    check("stream-zero-hole",done_bytes==9 && !memcmp(readback,expected_gap,9));
    done_bytes=123;status("stream-real-end",ScStreamRead(stream,readback,1,&done_bytes),SC_UTIL_END);check("stream-end-zero",done_bytes==0);
    ScStreamInfo stream_info;record(&stream_info,sizeof(stream_info));status("stream-counters",ScStreamQuery(stream,&stream_info),0);
    check("stream-counter-values",stream_info.bytes_read_lo==9 && !stream_info.bytes_read_hi && stream_info.bytes_written_lo==3);
    ScStreamRelease(stream);stream=NULL;ScBufferRelease(buffer);buffer=NULL;

    if(!status("binary-buffer",ScBufferCreate(0,&binary),0))goto done;
    ScWideCounter counter={0x89abcdefu,0x01020304u};
    status("binary-write-u8",ScBinaryWriteU8(binary,127),0);
    status("binary-write-u16",ScBinaryWriteU16LE(binary,0xbeefu),0);
    status("binary-write-u32",ScBinaryWriteU32LE(binary,0x12345678u),0);
    status("binary-write-counter",ScBinaryWriteCounterLE(binary,&counter),0);
    status("binary-write-var",ScBinaryWriteVarU32(binary,300),0);
    status("binary-write-utf8",ScBinaryWriteUtf8(binary,smile,4),0);
    const unsigned char expected[22]={127,239,190,120,86,52,18,239,205,171,137,4,3,2,1,172,2,4,240,159,153,130};
    record(&view,sizeof(view));status("binary-view",ScBufferView(binary,&view),0);
    check("binary-encoded-bytes",view.bytes==22 && !memcmp(view.data,expected,22));
    generation=view.generation;status("binary-reject-utf8",ScBinaryWriteUtf8(binary,bad_utf8,4),SC_UTIL_E_UTF8);
    ScView unchanged;record(&unchanged,sizeof(unchanged));status("binary-after-reject",ScBufferView(binary,&unchanged),0);
    check("binary-reject-keeps-record",unchanged.generation==generation && unchanged.bytes==22);
    ScBinaryReader reader;ScU32 value=0;status("binary-reader",ScBinaryReaderInit(&reader,view.data,view.bytes),0);
    status("binary-read-u8",ScBinaryReadU8(&reader,&value),0);check("binary-u8-value",value==127);
    status("binary-read-u16",ScBinaryReadU16LE(&reader,&value),0);check("binary-u16-value",value==0xbeefu);
    status("binary-read-u32",ScBinaryReadU32LE(&reader,&value),0);check("binary-u32-value",value==0x12345678u);
    ScWideCounter decoded={0,0};status("binary-read-counter",ScBinaryReadCounterLE(&reader,&decoded),0);
    check("binary-counter-value",decoded.lo==counter.lo && decoded.hi==counter.hi);
    status("binary-read-var",ScBinaryReadVarU32(&reader,&value),0);check("binary-var-value",value==300);
    ScView utf8;record(&utf8,sizeof(utf8));status("binary-read-utf8",ScBinaryReadUtf8(&reader,&utf8),0);
    check("binary-utf8-value",utf8.bytes==4 && !memcmp(utf8.data,smile,4) && reader.position==22);
    value=0x1234u;status("binary-bounds",ScBinaryReadU8(&reader,&value),SC_UTIL_E_RANGE);
    check("binary-bounds-preserves",value==0x1234u && reader.position==22);
    const unsigned char noncanonical[2]={128,0};status("binary-bad-reader",ScBinaryReaderInit(&reader,noncanonical,2),0);
    status("binary-canonical-var",ScBinaryReadVarU32(&reader,&value),SC_UTIL_E_SYNTAX);
    check("binary-var-failure-cursor",reader.position==0 && value==0x1234u);

    char cwd[65],filename[128];ScU32 cwd_bytes=0;
    if(!status("file-working-directory",ScProcessWorkingDirectory(cwd,sizeof(cwd),&cwd_bytes),0))goto done;
    if(!strncmp(cwd,"/HOME",5) && (!cwd[5] || cwd[5]=='/')){
        int length=snprintf(filename,sizeof(filename),"%s/UTILDATA.BIN",cwd);
        if(length<0 || length>=65){check("file-home-path-length",0);goto done;}
    }else{check("file-home-directory-required",0);goto done;}
    file=fopen(filename,"w+b");check("file-open",file!=NULL);if(!file)goto done;
    if(!status("stream-from-file",ScStreamFromFile(file,SC_STREAM_READ|SC_STREAM_WRITE|SC_STREAM_SEEK,&file_stream),0))goto done;
    status("file-binary-write",ScStreamWrite(file_stream,view.data,view.bytes,&done_bytes),0);check("file-write-count",done_bytes==22);
    status("file-actual-flush",ScStreamFlush(file_stream),0);
    status("file-rewind",ScStreamSeek(file_stream,0,SEEK_SET,&position),0);
    memset(readback,0,sizeof(readback));status("file-readback",ScStreamRead(file_stream,readback,22,&done_bytes),0);
    check("file-actual-bytes",done_bytes==22 && !memcmp(readback,expected,22));
done:
    if(file_stream)ScStreamRelease(file_stream);
    if(file)check("file-close",fclose(file)==0);
    if(stream)ScStreamRelease(stream);
    if(binary)ScBufferRelease(binary);
    if(buffer)ScBufferRelease(buffer);
    puts("UTIL buffer-binary-file finished");
}
static void errors_logs(void)
{
    ScErrorObject *error=NULL;ScLogger *logger=NULL;ScBuffer *buffer=NULL;ScStream *stream=NULL;ScString *formatted=NULL;
    ScErrorDesc desc;record(&desc,sizeof(desc));desc.code=SC_UTIL_E_SYNTAX;desc.domain=77;desc.raw=-17;desc.operation=33;desc.message="bad config";
    if(!status("error-create",ScErrorObjectCreate(&desc,&error),0))goto done;
    ScUtilErrorInfo info;record(&info,sizeof(info));status("error-query",ScErrorObjectQuery(error,&info),0);
    check("error-fields",info.code==SC_UTIL_E_SYNTAX && info.domain==77 && info.raw==-17 && info.operation==33);
    status("error-format",ScErrorObjectFormat(error,&formatted),0);
    const char *text=NULL;if(!status("error-format-cstr",ScStringCStr(formatted,&text),0))goto done;
    check("error-real-text",strstr(text,"bad config")!=NULL && strstr(text,"-211")!=NULL);
    if(!status("log-buffer",ScBufferCreate(0,&buffer),0))goto done;
    if(!status("log-stream",ScStreamFromBuffer(buffer,SC_STREAM_WRITE|SC_STREAM_SEEK,&stream),0))goto done;
    ScLogDesc log;record(&log,sizeof(log));log.minimum_level=SC_LOG_INFO;log.max_records=2;log.flags=SC_LOG_CLOCK;log.sink=stream;
    if(!status("log-create",ScLogCreate(&log,&logger),0))goto done;
    status("log-filtered",ScLogWrite(logger,SC_LOG_TRACE,0,"hidden",6),0);
    status("log-format",ScLogFormat(logger,SC_LOG_INFO,7,"item=%u",42u),0);
    status("log-warning",ScLogWrite(logger,SC_LOG_WARNING,-3,"warn\nX",6),0);
    ScContainerInfo count;record(&count,sizeof(count));status("log-count",ScLogQuery(logger,&count),0);check("log-filter-count",count.length==2);
    ScLogEntry entry;record(&entry,sizeof(entry));status("log-entry",ScLogGet(logger,0,&entry),0);
    check("log-entry-text",entry.sequence_lo==1 && entry.sequence_hi==0 && entry.text.bytes==7 && !memcmp(entry.text.data,"item=42",7));
    status("log-flush",ScLogFlush(logger),0);
    ScView view;record(&view,sizeof(view));status("log-sink-view",ScBufferView(buffer,&view),0);
    char output[512];if(view.bytes<sizeof(output)){memcpy(output,view.data,view.bytes);output[view.bytes]=0;}
    else{check("log-small-output",0);goto done;}
    check("log-real-sink",strstr(output,"item=42")!=NULL && strstr(output,"warn\\nX")!=NULL);
    ScU32 first_bytes=view.bytes,old_generation=entry.text.generation;
    status("log-flush-no-repeat",ScLogFlush(logger),0);
    record(&view,sizeof(view));status("log-second-sink",ScBufferView(buffer,&view),0);check("log-no-duplicate",view.bytes==first_bytes);
    status("log-bounded-evict",ScLogFormat(logger,SC_LOG_ERROR,9,"again"),0);
    record(&count,sizeof(count));status("log-new-count",ScLogQuery(logger,&count),0);
    check("log-bounded-stale",count.length==2 && count.generation!=old_generation);
    record(&entry,sizeof(entry));status("log-oldest-now-warning",ScLogGet(logger,0,&entry),0);check("log-evicted-sequence",entry.sequence_lo==2);
    status("log-last-flush",ScLogFlush(logger),0);
done:
    if(logger)ScLogRelease(logger);
    if(stream)ScStreamRelease(stream);
    if(buffer)ScBufferRelease(buffer);
    if(formatted)ScStringRelease(formatted);
    if(error)ScErrorObjectRelease(error);
    puts("UTIL error-log finished");
}
typedef struct {ScEventHub *hub;ScU32 next,late,first_calls,next_calls,late_calls,drops;ScEvent nested;} EventState;
static void drop_subscription(void *context,void *ignored)
{EventState *state=context;(void)ignored;state->drops++;}
static ScStatus event_late(const ScEvent *event,void *context)
{EventState *state=context;state->late_calls++;ScU32 value=0;memcpy(&value,event->data,4);return value==7?0:SC_UTIL_E_INVALID;}
static ScStatus event_next(const ScEvent *event,void *context)
{EventState *state=context;(void)event;state->next_calls++;return 0;}
static ScStatus event_first(const ScEvent *event,void *context)
{
    EventState *state=context;state->first_calls++;
    if(event->type!=77 || event->bytes!=4)return SC_UTIL_E_INVALID;
    ScSubscriptionDesc desc;record(&desc,sizeof(desc));desc.type=77;desc.priority=5;
    desc.procedure=event_late;desc.context=state;desc.context_drop=drop_subscription;
    ScStatus result=ScEventConnect(state->hub,&desc,&state->late);if(result)return result;
    result=ScEventDisconnect(state->hub,state->next);if(result)return result;
    if(state->drops)return SC_UTIL_E_INVALID;
    ScEventResult nested;record(&nested,sizeof(nested));result=ScEventEmit(state->hub,&state->nested,0,&nested);
    if(result || nested.called!=1 || state->first_calls!=1 || state->drops)return SC_UTIL_E_INVALID;
    return 0;
}
static void events(void)
{
    EventState state;memset(&state,0,sizeof(state));ScU32 payload=7;
    record(&state.nested,sizeof(state.nested));state.nested.type=77;state.nested.data=&payload;state.nested.bytes=4;
    if(!status("event-hub",ScEventHubCreate(&state.hub),0))return;
    status("persistent-app-callback-pin",ScCallbackPin((const void *)event_first),0);
    ScSubscriptionDesc desc;record(&desc,sizeof(desc));desc.type=77;desc.priority=10;desc.flags=SC_EVENT_ONCE;
    desc.procedure=event_first;desc.context=&state;desc.context_drop=drop_subscription;
    ScU32 first=0;status("event-once-connect",ScEventConnect(state.hub,&desc,&first),0);
    desc.flags=0;desc.priority=0;desc.procedure=event_next;status("event-next-connect",ScEventConnect(state.hub,&desc,&state.next),0);
    ScEventResult result;record(&result,sizeof(result));status("event-reentrant-emit",ScEventEmit(state.hub,&state.nested,0,&result),0);
    check("event-watermark-once",result.called==1 && state.first_calls==1 && state.next_calls==0 && state.late_calls==1 && state.drops==2);
    record(&result,sizeof(result));status("event-next-emit",ScEventEmit(state.hub,&state.nested,0,&result),0);
    check("event-new-subscriber",result.called==1 && state.late_calls==2 && !result.first_error);
    ScEventHubRelease(state.hub);state.hub=NULL;
    check("event-context-drops",state.drops==3);
    printf("UTIL events first=%u late=%u drop=%u\n",state.first_calls,state.late_calls,state.drops);
}
typedef struct {ScModel *model;ScU32 a,b,c,late,notices,kinds,visit_count,visited[4];int reject_value;} ModelState;
static ScStatus model_notice(const ScEvent *event,void *context)
{
    ModelState *state=context;if(event->bytes!=sizeof(ScModelChange) || !event->data)return SC_UTIL_E_INVALID;
    ScModelChange change;memcpy(&change,event->data,sizeof(change));state->notices++;state->kinds|=1u<<change.kind;
    ScModelNodeInfo info;record(&info,sizeof(info));ScStatus result=ScModelNodeQuery(state->model,change.node,&info);
    if(change.kind==SC_MODEL_REMOVE){if(result!=SC_UTIL_E_STALE)return SC_UTIL_E_INVALID;}
    else if(result || info.parent!=change.parent)return SC_UTIL_E_INVALID;
    if(change.kind==SC_MODEL_VALUE && state->reject_value){state->reject_value=0;return SC_UTIL_E_CANCELLED;}
    return 0;
}
static ScStatus model_visit(ScU32 node,const ScModelNodeInfo *info,void *context)
{
    ModelState *state=context;if(state->visit_count>=4 || info->id!=node)return SC_UTIL_E_INVALID;
    state->visited[state->visit_count++]=node;
    if(!node)return ScModelInsert(state->model,state->a,0,&state->late);
    return 0;
}
static void models(void)
{
    ModelState state;memset(&state,0,sizeof(state));
    if(!status("model-create",ScModelCreate(&state.model),0))return;
    ScSubscriptionDesc desc;record(&desc,sizeof(desc));desc.procedure=model_notice;desc.context=&state;
    ScU32 token=0;status("model-observer",ScModelConnect(state.model,&desc,&token),0);
    status("model-parent-a",ScModelInsert(state.model,0,0,&state.a),0);
    status("model-child-b",ScModelInsert(state.model,state.a,0,&state.b),0);
    status("model-parent-c",ScModelInsert(state.model,0,0,&state.c),0);
    ScValue value;record(&value,sizeof(value));value.type=SC_VALUE_UTF8;value.data="node";value.bytes=4;
    state.reject_value=1;status("model-value-commit",ScModelSet(state.model,state.b,"title",&value),0);
    ScStatus notification=0;status("model-observer-status",ScModelNotificationStatus(state.model,&notification),0);
    check("model-observer-failure-separate",notification==SC_UTIL_E_CANCELLED);
    record(&value,sizeof(value));status("model-committed-value",ScModelGet(state.model,state.b,"title",&value),0);
    check("model-value-visible",value.type==SC_VALUE_UTF8 && value.bytes==4 && !memcmp(value.data,"node",4));
    status("model-move-child",ScModelMove(state.model,state.b,state.c,0),0);
    ScModelNodeInfo before;record(&before,sizeof(before));status("model-before-cycle",ScModelNodeQuery(state.model,state.c,&before),0);
    ScU32 notices=state.notices;
    /* 若这个负例竟然提交，不能再遍历可能已经有环的图来掩盖首个错误。 */
    if(!status("model-cycle-reject",ScModelMove(state.model,state.c,state.b,0),SC_UTIL_E_INVALID))goto done;
    ScModelNodeInfo after;record(&after,sizeof(after));status("model-after-cycle",ScModelNodeQuery(state.model,state.c,&after),0);
    check("model-cycle-keeps-tree",before.parent==after.parent && before.revision==after.revision && notices==state.notices);
    status("model-snapshot-visit",ScModelVisit(state.model,0,model_visit,&state),0);
    check("model-visit-watermark",state.visit_count==4 && state.visited[0]==0 && state.visited[1]==state.a && state.visited[2]==state.c && state.visited[3]==state.b && state.late!=0);
    status("model-remove-subtree",ScModelRemove(state.model,state.c),0);
    record(&after,sizeof(after));status("model-child-stale",ScModelNodeQuery(state.model,state.b,&after),SC_UTIL_E_STALE);
    check("model-change-kinds",state.kinds==30 && state.notices==7);
done:
    if(token)status("model-disconnect",ScModelDisconnect(state.model,token),0);
    ScModelRelease(state.model);state.model=NULL;
    printf("UTIL model notices=%u visited=%u\n",state.notices,state.visit_count);
}
typedef struct {ScU32 loaded,dropped,oom;int fail_next;} CacheState;
static ScStatus cache_load(const ScView *key,const ScCacheStamp *stamp,void *context,void **resource,ScU32 *cost)
{
    CacheState *state=context;
    if(state->fail_next){
        state->fail_next=0;errno=0;
        /* 当前heap64MiB连metadata也计容量，此真实payload请求无法满足。
         * 不耗尽VM物理页，不给库写注入/占位分支。 */
        void *huge=malloc(0x04000000u);
        if(huge){free(huge);return SC_UTIL_E_INVALID;}
        state->oom=errno==ENOMEM;*resource=NULL;*cost=0;return SC_UTIL_E_MEMORY;
    }
    if(key->bytes!=1)return SC_UTIL_E_INVALID;
    ScU32 *value=malloc(sizeof(*value));if(!value)return SC_UTIL_E_MEMORY;
    *value=((ScU32)((const unsigned char *)key->data)[0]<<16)|stamp->object_generation;
    state->loaded++;*resource=value;*cost=4;return 0;
}
static void cache_drop(void *resource,void *context)
{CacheState *state=context;state->dropped++;free(resource);}
static void caches(void)
{
    CacheState state={0,0,0,0};ScResourceCache *cache=NULL,*closing=NULL;
    void *old=NULL,*fresh=NULL,*hit=NULL;ScU32 old_lease=0,new_lease=0,hit_lease=0;
    ScCacheDesc desc;record(&desc,sizeof(desc));desc.budget_bytes=1024;desc.load=cache_load;desc.drop=cache_drop;desc.context=&state;
    if(!status("cache-create",ScCacheCreate(&desc,&cache),0))return;
    ScCacheStamp stamp;record(&stamp,sizeof(stamp));stamp.namespace_id=7;stamp.object_id=9;stamp.object_generation=1;stamp.theme_epoch=2;stamp.display_epoch=3;stamp.user_generation=4;
    if(!status("cache-first",ScCacheAcquire(cache,"a",1,&stamp,&old,&old_lease),0))goto done;
    ScCacheInfo info;record(&info,sizeof(info));if(!status("cache-charge",ScCacheQuery(cache,&info),0))goto done;
    ScU32 charge=info.used_bytes;check("cache-metadata-budget",charge>4 && charge<=512 && info.entries==1 && info.leases==1);
    if(!status("cache-two-budget",ScCacheSetBudget(cache,charge*2u),0))goto done;
    stamp.object_generation=2;
    if(!status("cache-stamp-replacement",ScCacheAcquire(cache,"a",1,&stamp,&fresh,&new_lease),0))goto done;
    check("cache-held-old-resource",old!=fresh && *(ScU32 *)old==0x00610001u && *(ScU32 *)fresh==0x00610002u && state.dropped==0);
    if(!status("cache-old-lease-release",ScCacheReleaseLease(cache,old_lease),0))goto done;
    old_lease=0;
    check("cache-old-drop",state.dropped==1);
    if(!status("cache-tight-budget",ScCacheSetBudget(cache,charge),0))goto done;
    ScCacheStamp newer=stamp;newer.object_generation=3;
    void *candidate=&charge;ScU32 candidate_lease=0x12345678u;
    status("cache-pinned-budget-failure",ScCacheAcquire(cache,"a",1,&newer,&candidate,&candidate_lease),SC_UTIL_E_BUSY);
    check("cache-busy-candidate-dropped",candidate==&charge && candidate_lease==0x12345678u && state.dropped==2 && *(ScU32 *)fresh==0x00610002u);
    if(!status("cache-new-lease-release",ScCacheReleaseLease(cache,new_lease),0))goto done;
    new_lease=0;
    record(&info,sizeof(info));status("cache-before-oom",ScCacheQuery(cache,&info),0);
    ScU32 revision=info.revision;state.fail_next=1;
    status("cache-real-oom",ScCacheAcquire(cache,"a",1,&newer,&candidate,&candidate_lease),SC_UTIL_E_MEMORY);
    check("cache-oom-output-preserved",state.oom && candidate==&charge && candidate_lease==0x12345678u && state.dropped==2);
    record(&info,sizeof(info));status("cache-after-oom",ScCacheQuery(cache,&info),0);
    check("cache-oom-keeps-entry",info.revision==revision && info.entries==1 && info.used_bytes==charge);
    ScU32 loaded=state.loaded;
    if(!status("cache-old-stamp-still-hit",ScCacheAcquire(cache,"a",1,&stamp,&hit,&hit_lease),0))goto done;
    check("cache-hit-value",hit==fresh && state.loaded==loaded && *(ScU32 *)hit==0x00610002u);
    if(!status("cache-hit-release",ScCacheReleaseLease(cache,hit_lease),0))goto done;
    hit_lease=0;
    if(!status("cache-budget-restore",ScCacheSetBudget(cache,charge*2u),0))goto done;
    if(!status("cache-b",ScCacheAcquire(cache,"b",1,&stamp,&hit,&hit_lease),0))goto done;
    if(!status("cache-b-release",ScCacheReleaseLease(cache,hit_lease),0))goto done;
    hit_lease=0;
    if(!status("cache-c-evicts-lru",ScCacheAcquire(cache,"c",1,&stamp,&hit,&hit_lease),0))goto done;
    if(!status("cache-c-release",ScCacheReleaseLease(cache,hit_lease),0))goto done;
    hit_lease=0;
    record(&info,sizeof(info));status("cache-lru-info",ScCacheQuery(cache,&info),0);
    check("cache-lru-budget",info.entries==2 && info.used_bytes==charge*2u && info.evictions.lo==1 && state.dropped==3);
    loaded=state.loaded;if(!status("cache-b-survived",ScCacheAcquire(cache,"b",1,&stamp,&hit,&hit_lease),0))goto done;
    check("cache-b-hit",state.loaded==loaded && *(ScU32 *)hit==0x00620002u);
    status("cache-invalidate-held",ScCacheInvalidate(cache,"b",1),0);
    check("cache-invalidate-retains-lease",*(ScU32 *)hit==0x00620002u && state.dropped==3);
    if(!status("cache-invalidated-release",ScCacheReleaseLease(cache,hit_lease),0))goto done;
    hit_lease=0;
    check("cache-invalidated-drop",state.dropped==4);
    if(!status("cache-last-held",ScCacheAcquire(cache,"c",1,&stamp,&hit,&hit_lease),0))goto done;
    closing=cache;cache=NULL;ScCacheRelease(closing);
    check("cache-release-keeps-held",*(ScU32 *)hit==0x00630002u && state.dropped==4);
    /* CacheRelease后header由最后lease保留，ReleaseLease是唯一合法收尾入口。 */
    if(!status("cache-final-lease",ScCacheReleaseLease(closing,hit_lease),0))goto done;
    closing=NULL;hit_lease=0;
    check("cache-all-resources-dropped",state.loaded==5 && state.loaded==state.dropped && state.oom==1);
done:
    if(cache){
        if(old_lease)ScCacheReleaseLease(cache,old_lease);
        if(new_lease)ScCacheReleaseLease(cache,new_lease);
        if(hit_lease)ScCacheReleaseLease(cache,hit_lease);
        ScCacheRelease(cache);
    }
    if(closing && hit_lease)ScCacheReleaseLease(closing,hit_lease);
    printf("UTIL cache load=%u drop=%u oom=%u\n",state.loaded,state.dropped,state.oom);
}
int main(void)
{
    void *warm=malloc(16);check("heap-warm",warm!=NULL);free(warm);
    puts("UTIL BEGIN v1");fflush(stdout);
    ScMemoryInfo before;record(&before,sizeof(before));status("memory-before",ScSystemMemory(&before),0);
    containers();strings_paths_config();buffers_binary_file();errors_logs();events();models();caches();
    ScMemoryInfo after;record(&after,sizeof(after));status("memory-after",ScSystemMemory(&after),0);
    check("normal-main-heap-page-balance",before.process_heap_pages==after.process_heap_pages && before.process_module_pages==after.process_module_pages);
    if(failures)printf("UTIL FAIL TOTAL checks=%u failed=%u\n",checks,failures);
    else printf("UTIL PASS checks=%u heap=%u modules=%u\n",checks,after.process_heap_pages,after.process_module_pages);
    if(fflush(stdout))return 1;
    return failures?1:0;
}
