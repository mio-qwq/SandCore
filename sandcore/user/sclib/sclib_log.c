#include "SCLIBPRIVATE.H"

typedef struct {
    ScU32 level;
    ScI32 code;
    ScWideCounter tick,sequence;
    ScString *text,*line;
    ScU32 sent;
} SlLogRecord;
struct ScLogger {SlBase base;
    SlLogRecord **records;
    ScU32 first,count,capacity,minimum,maximum,flags,generation;
    ScStream *sink;
    ScWideCounter sequence;};
static void sl_log_record_free(SlLogRecord *record)
{if(record->text)ScStringRelease(record->text);
    if(record->line)ScStringRelease(record->line);
    free(record);}
static void sl_log_dispose(SlBase *base,int quiet)
{
    ScLogger *logger=(ScLogger *)base;
    (void)quiet;
    for(ScU32 i=0;i<logger->count;i++)sl_log_record_free(logger->records[(logger->first+i)%logger->capacity]);
    free(logger->records);
    logger->records=0;
    logger->count=logger->capacity=0;
    if(logger->sink)ScStreamRelease(logger->sink);
    logger->sink=0;
}
ScStatus ScLogCreate(const ScLogDesc *desc,ScLogger **out)
{
    ScStatus status=sl_input(desc,sizeof(*desc));
    if(status || !out)return status?status:SC_UTIL_E_INVALID;
    if(desc->minimum_level>SC_LOG_ERROR || (desc->flags&~SC_LOG_CLOCK) || desc->reserved[0] || desc->reserved[1])return SC_UTIL_E_INVALID;
    if(desc->sink){status=sl_valid(desc->sink,SL_STREAM);
        if(status)return status;}
    ScLogger *logger=sl_object(SL_LOG,sizeof(*logger),sl_log_dispose);
    if(!logger)return SC_UTIL_E_MEMORY;
    if(desc->sink){status=ScStreamRetain(desc->sink);
        if(status){ScLogRelease(logger);
            return status;}logger->sink=desc->sink;}
    logger->minimum=desc->minimum_level;
    logger->maximum=desc->max_records;
    logger->flags=desc->flags;
    logger->generation=1;
    *out=logger;
    return 0;
}
ScStatus ScLogRetain(ScLogger *logger){return sl_retain(logger,SL_LOG);}
void ScLogRelease(ScLogger *logger){sl_release(logger,SL_LOG);}
static ScStatus sl_log_line(SlLogRecord *record)
{
    if(record->line)return 0;
    ScString *line=0,*escaped;
    ScStatus status=ScFormatEscape(record->text->data,record->text->bytes,&escaped);
    if(status)return status;
    status=ScStringCreate(0,0,&line);
    if(!status)status=ScStringAppendFormat(line,"[%u:%u] %u %d ",record->tick.hi,record->tick.lo,record->level,record->code);
    if(!status)status=ScStringAppend(line,escaped->data,escaped->bytes);
    if(!status)status=ScStringAppend(line,"\n",1);
    ScStringRelease(escaped);
    if(status){if(line)ScStringRelease(line);
        return status;}
    record->line=line;
    return 0;
}
static ScStatus sl_log_accept(ScLogger *logger,ScU32 level,ScI32 code,ScString *text)
{
    ScStatus status=sl_mutation(&logger->base,logger->generation);
    if(status)return status;
    if(logger->sequence.lo==0xFFFFFFFFu && logger->sequence.hi==0xFFFFFFFFu)return SC_UTIL_E_RANGE;
    if(logger->count==0xFFFFFFFFu)return SC_UTIL_E_RANGE;
    int evict=logger->maximum && logger->count==logger->maximum;
    if(evict && logger->sink){
        SlLogRecord *old=logger->records[logger->first];
        if(!old->line || old->sent<old->line->bytes)return SC_UTIL_E_BUSY;
    }
    SlLogRecord *record=calloc(1,sizeof(*record));
    if(!record)return SC_UTIL_E_MEMORY;
    record->level=level;
    record->code=code;
    if(logger->flags&SC_LOG_CLOCK){
        ScMonotonicClock clock;
        clock.size=sizeof(clock);
        clock.version=1;
        status=ScSystemMonotonic(&clock);
        if(status){free(record);
            return status;}record->tick=clock.ticks;
    }
    ScU32 wanted=logger->count+(evict?0u:1u),capacity=logger->capacity;
    SlLogRecord **fresh=0;
    if(wanted>capacity){
        capacity=sl_capacity(capacity,wanted,sizeof(*fresh));
        if(logger->maximum && capacity>logger->maximum)capacity=logger->maximum;
        if(!capacity){free(record);
            return SC_UTIL_E_RANGE;}
        fresh=calloc(capacity,sizeof(*fresh));
        if(!fresh){free(record);
            return SC_UTIL_E_MEMORY;}
        for(ScU32 i=0;i<logger->count;i++)fresh[i]=logger->records[(logger->first+i)%logger->capacity];
    }
    if(fresh){free(logger->records);
        logger->records=fresh;
        logger->capacity=capacity;
        logger->first=0;}
    if(evict){sl_log_record_free(logger->records[logger->first]);
        logger->first=(logger->first+1u)%logger->capacity;
        logger->count--;}
    record->text=text;
    sl_counter_add(&logger->sequence,1);
    record->sequence=logger->sequence;
    logger->records[(logger->first+logger->count)%logger->capacity]=record;
    logger->count++;
    logger->generation++;
    return 0;
}
ScStatus ScLogWrite(ScLogger *logger,ScU32 level,ScI32 code,const void *utf8,ScU32 bytes)
{
    ScStatus status=sl_valid(logger,SL_LOG);
    if(status)return status;
    if(level>SC_LOG_ERROR)return SC_UTIL_E_INVALID;
    if(level<logger->minimum)return 0;
    status=ScUtf8Validate(utf8,bytes,0);
    if(status)return status;
    ScString *text;
    status=ScStringCreate(utf8,bytes,&text);
    if(status)return status;
    status=sl_log_accept(logger,level,code,text);
    if(status)ScStringRelease(text);
    return status;
}
ScStatus ScLogVFormat(ScLogger *logger,ScU32 level,ScI32 code,const char *format,va_list arguments)
{
    ScStatus status=sl_valid(logger,SL_LOG);
    if(status)return status;
    if(level>SC_LOG_ERROR)return SC_UTIL_E_INVALID;
    if(level<logger->minimum)return 0;
    ScString *text;
    status=ScStringCreate(0,0,&text);
    if(status)return status;
    status=ScStringVFormat(text,format,arguments);
    if(!status)status=ScUtf8Validate(text->data,text->bytes,0);
    if(!status)status=sl_log_accept(logger,level,code,text);
    if(status)ScStringRelease(text);
    return status;
}
ScStatus ScLogFormat(ScLogger *logger,ScU32 level,ScI32 code,const char *format,...)
{va_list arguments;
    va_start(arguments,format);
    ScStatus status=ScLogVFormat(logger,level,code,format,arguments);
    va_end(arguments);
    return status;}
ScStatus ScLogGet(const ScLogger *logger,ScU32 index,ScLogEntry *out)
{
    ScStatus status=sl_valid(logger,SL_LOG);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    if(index>=logger->count)return SC_UTIL_E_RANGE;
    SlLogRecord *record=logger->records[(logger->first+index)%logger->capacity];
    ScLogEntry value;
    memset(&value,0,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    value.level=record->level;
    value.code=record->code;
    value.tick=record->tick;
    value.sequence_lo=record->sequence.lo;
    value.sequence_hi=record->sequence.hi;
    sl_view(&value.text,record->text->data,record->text->bytes,logger->generation);
    memcpy(out,&value,sizeof(value));
    return 0;
}
ScStatus ScLogFlush(ScLogger *logger)
{
    ScStatus status=sl_enter(logger,SL_LOG);
    if(status)return status;
    if(logger->base.mutation){sl_leave(logger);
        return SC_UTIL_E_BUSY;}
    if(!logger->sink){sl_leave(logger);
        return 0;}
    logger->base.mutation=1;
    for(ScU32 i=0;i<logger->count && !status;i++){
        SlLogRecord *record=logger->records[(logger->first+i)%logger->capacity];
        status=sl_log_line(record);
        if(status)break;
        while(record->sent<record->line->bytes && !status){
            ScU32 done=0;
            status=ScStreamWrite(logger->sink,record->line->data+record->sent,record->line->bytes-record->sent,&done);
            record->sent+=done;
            if(!status && !done)status=SC_UTIL_E_AGAIN;
            if(logger->base.closing){if(!status)status=SC_UTIL_E_CANCELLED;
                break;}
        }
    }
    if(!status)status=ScStreamFlush(logger->sink);
    logger->base.mutation=0;
    sl_leave(logger);
    return status;
}
ScStatus ScLogQuery(const ScLogger *logger,ScContainerInfo *out)
{
    ScStatus status=sl_valid(logger,SL_LOG);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    sl_info(out,logger->count,logger->capacity,sizeof(ScLogEntry),logger->generation,logger->flags);
    return 0;
}
ScStatus ScLogClear(ScLogger *logger)
{
    ScStatus status=sl_valid(logger,SL_LOG);
    if(status)return status;
    status=sl_mutation(&logger->base,logger->generation);
    if(status)return status;
    if(!logger->count)return 0;
    for(ScU32 i=0;i<logger->count;i++)sl_log_record_free(logger->records[(logger->first+i)%logger->capacity]);
    logger->count=logger->first=0;
    logger->generation++;
    return 0;
}
