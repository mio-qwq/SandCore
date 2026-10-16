#include "SC32PRIVATE.H"

static void *sc32_path_scratch;
ScStatus ScPathResolve(const char *path,char *out,ScU32 capacity,ScU32 *length)
{
    ScStatus checked=sc32_text_buffers(out,capacity,length);if(checked<0)return sc32_invalid(checked,SC32_OP_RESOLVE);
    char absolute[65];checked=sc32_absolute(path,absolute,SC32_OP_RESOLVE);if(checked<0)return checked;
    ScU32 bytes=0;while(absolute[bytes])bytes++;return sc32_text_output(absolute,bytes,out,capacity,length,SC32_OP_RESOLVE);
}
ScStatus ScPathStat(const char *path,ScPathStatInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScPathStatInfo));if(checked<0)return sc32_invalid(checked,SC32_OP_STAT);
    char absolute[65];checked=sc32_absolute(path,absolute,SC32_OP_STAT);if(checked<0)return checked;
    ScU32 raw[2];ScI32 status=sc32_call(0x37,(ScI32)absolute,(ScI32)raw,0,0,0,0);
    if(status<0)return sc32_legacy(status,SC32_OP_STAT);
    ScPathStatInfo value={16,1,raw[0],raw[1]};sc32_copy(out,&value,sizeof(value));return 0;
}
ScStatus ScPathMetadata(const char *path,ScPathInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScPathInfo));if(checked<0)return sc32_invalid(checked,SC32_OP_META);
    char absolute[65];checked=sc32_absolute(path,absolute,SC32_OP_META);if(checked<0)return checked;
    ScU32 raw[8];ScI32 status=sc32_call(0x208,(ScI32)absolute,(ScI32)raw,0,0,0,0);
    if(status<0)return sc32_legacy(status,SC32_OP_META);
    ScPathInfo value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
    value.kind=raw[1];value.bytes=raw[2];value.uid=(ScI32)raw[3];value.gid=(ScI32)raw[4];
    value.rw=raw[5];value.flags=raw[6];value.generation=raw[7];sc32_copy(out,&value,sizeof(value));return 0;
}
ScStatus ScPathCreate(const char *path,ScU32 kind,ScU32 rw)
{
    if((kind!=SC32_PATH_FILE && kind!=SC32_PATH_DIRECTORY) || rw>63)return sc32_invalid(SC32_E_INVALID,SC32_OP_CREATE);
    char absolute[65];ScStatus checked=sc32_absolute(path,absolute,SC32_OP_CREATE);if(checked<0)return checked;
    ScI32 raw=sc32_call(0x239,(ScI32)absolute,(ScI32)kind,(ScI32)rw,0,0,0);
    if(raw==-8)return sc32_fail(SC32_E_EXISTS,SC32_DOMAIN_LEGACY,raw,SC32_OP_CREATE);
    if(raw==-6)return sc32_fail(SC32_E_BUSY,SC32_DOMAIN_LEGACY,raw,SC32_OP_CREATE);
    return sc32_legacy(raw,SC32_OP_CREATE);
}
ScStatus ScPathRemove(const char *path)
{
    char absolute[65];ScStatus checked=sc32_absolute(path,absolute,SC32_OP_REMOVE);if(checked<0)return checked;
    return sc32_legacy(sc32_call(0x35,(ScI32)absolute,0,0,0,0,0),SC32_OP_REMOVE);
}
ScStatus ScPathRename(const char *before,const char *after)
{
    char first[65],second[65];ScStatus checked=sc32_absolute(before,first,SC32_OP_RENAME);if(checked<0)return checked;
    checked=sc32_absolute(after,second,SC32_OP_RENAME);if(checked<0)return checked;
    ScI32 raw=sc32_call(0x36,(ScI32)first,(ScI32)second,0,0,0,0);
    return raw==-6?sc32_fail(SC32_E_BUSY,SC32_DOMAIN_LEGACY,raw,SC32_OP_RENAME):sc32_legacy(raw,SC32_OP_RENAME);
}
ScStatus ScPathPermissions(const char *path,ScI32 uid,ScI32 gid,ScU32 rw)
{
    if(uid<-2 || gid<-2 || rw>63)return sc32_invalid(SC32_E_INVALID,SC32_OP_PERMISSIONS);
    char absolute[65];ScStatus checked=sc32_absolute(path,absolute,SC32_OP_PERMISSIONS);if(checked<0)return checked;
    return sc32_legacy(sc32_call(0x209,(ScI32)absolute,uid,gid,(ScI32)rw,0,0),SC32_OP_PERMISSIONS);
}
ScStatus ScPathRead(const char *path,ScU32 offset,void *out,ScU32 capacity,ScU32 *transferred)
{
    ScStatus checked=sc32_out(transferred,4);if(checked<0)return sc32_invalid(checked,SC32_OP_PATH_READ);
    if(capacity>SC32_PATH_IO_MAX)return sc32_invalid(SC32_E_RANGE,SC32_OP_PATH_READ);
    checked=sc32_out(out,capacity);if(checked<0)return sc32_invalid(checked,SC32_OP_PATH_READ);
    if(sc32_overlap(out,capacity,transferred,4))return sc32_invalid(SC32_E_INVALID,SC32_OP_PATH_READ);
    char absolute[65];checked=sc32_absolute(path,absolute,SC32_OP_PATH_READ);if(checked<0)return checked;
    if(capacity && !sc32_path_scratch){
        ScU32 scratch=(ScU32)sc32_call(0x100,SC32_PATH_IO_MAX,0,0,0,0,0);
        if(!scratch)return sc32_fail(SC32_E_MEMORY,SC32_DOMAIN_LEGACY,0,SC32_OP_PATH_READ);
        sc32_path_scratch=(void *)scratch;
    }
    /* 旧READAT可能先写一段再报IO错误却不给前缀长度。先读库所有的
     * 64KiB复用缓冲，完整成功才发表调用者输出，错误不猜已读长度。 */
    unsigned char empty=0;void *target=capacity?sc32_path_scratch:&empty;
    ScI32 raw=sc32_call(0x38,(ScI32)absolute,(ScI32)target,(ScI32)capacity,(ScI32)offset,0,0);
    if(raw<0)return sc32_legacy(raw,SC32_OP_PATH_READ);
    if((ScU32)raw>capacity)return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_LEGACY,raw,SC32_OP_PATH_READ);
    if(raw)sc32_copy(out,target,(ScU32)raw);
    *transferred=(ScU32)raw;return 0;
}
ScStatus ScPathReplace(const char *path,const void *data,ScU32 bytes,ScU32 *transferred)
{
    ScStatus checked=sc32_out(transferred,4);if(checked<0)return sc32_invalid(checked,SC32_OP_PATH_REPLACE);
    if(bytes>SC32_PATH_IO_MAX)return sc32_invalid(SC32_E_RANGE,SC32_OP_PATH_REPLACE);
    checked=sc32_probe(data,bytes,SC32_WIRE_READ);if(checked<0)return sc32_invalid(checked,SC32_OP_PATH_REPLACE);
    if(sc32_overlap(data,bytes,transferred,4))return sc32_invalid(SC32_E_INVALID,SC32_OP_PATH_REPLACE);
    char absolute[65];checked=sc32_absolute(path,absolute,SC32_OP_PATH_REPLACE);if(checked<0)return checked;
    unsigned char empty=0;const void *source=bytes?data:&empty;
    ScI32 raw=sc32_call(0x31,(ScI32)absolute,(ScI32)source,(ScI32)bytes,0,0,0);
    if(raw<0)return sc32_legacy(raw,SC32_OP_PATH_REPLACE);
    *transferred=(ScU32)raw;return 0;
}
/* runtime只在main及用户回调结束后调用fini；异常退出由内核收私有页。
 * 此缓存按库实例归属，不用逐次分配64KiB，也不借另一个进程的缓冲。 */
void Sc32Finalize(void)
{
    sc32_image_finalize();
    sc32_auth_finalize();
    sc32_gui_finalize();
    if(sc32_path_scratch){sc32_call(0x101,(ScI32)sc32_path_scratch,0,0,0,0,0);sc32_path_scratch=0;}
}
