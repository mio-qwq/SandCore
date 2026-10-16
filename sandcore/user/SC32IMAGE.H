#ifndef SANDCORE_SC32IMAGE_H
#define SANDCORE_SC32IMAGE_H
#include "SC32API.H"

/* 既有独立Ring3图片服务的typed入口；请求只属于实际调用进程/代数，
 * 不是worker句柄或内核地址。旧IMAGEREQUEST/RESULT/CANCEL语义不动。 */
typedef ScU32 ScImageRequest;
enum {SC32_IMAGE_ARGB32=2,SC32_IMAGE_MAX_WIDTH=1920,SC32_IMAGE_MAX_HEIGHT=1080,
    SC32_IMAGE_READY=1,SC32_IMAGE_MORE_WORK=2,SC32_IMAGE_SERVICE=4,SC32_IMAGE_INPUT_SEALED=8,
    SC32_IMAGE_MAX_SOURCE_BYTES=16u*1024u*1024u};
typedef struct {ScU32 size,version;const char *path;ScU32 flags,reserved[4];} ScImageDecodeDesc;
typedef struct {ScU32 size,version;const void *data;ScU32 bytes,flags,reserved[3];} ScImageMemoryDesc;
typedef struct {
    ScU32 size,version,request,width,height,stride,bytes,format;
    ScU32 flags,progress,total,wait_events;
} ScImageInfo;
ScStatus ScImageDecodeBegin(const ScImageDecodeDesc *,ScImageRequest *out);
/* 内存输入只借用，不在Begin复制整幅文件。调用者保持data存活且不变，
 * 直到Step输出INPUT_SEALED，或请求失败/Cancel。后续服务只读自有封存页。
 * 支持真实PNG/JPEG/WebP/BMP；不产生临时文件，不保存跨调用用户指针到内核。 */
ScStatus ScImageDecodeMemoryBegin(const ScImageMemoryDesc *,ScImageRequest *out);
/* byte_budget为1..65536；memory输入每次至多搬运该预算，PENDING照常
 * 输出Info。Query本身不搬运memory输入；封存后Step等同Query。 */
ScStatus ScImageDecodeStep(ScImageRequest,ScU32 byte_budget,ScImageInfo *out);
/* PENDING也输出有效Info。MORE_WORK表示本次Query推进<=64KiB SCB
 * 读取/全槽验证，可在泵输入后继续；SERVICE表示等wait_events，
 * 不伪造解码进度（progress/total均0）。READY查询仍不消费票据。 */
ScStatus ScImageDecodeQuery(ScImageRequest,ScImageInfo *out);
ScStatus ScImageDecodeCopy(ScImageRequest,void *pixels,ScU32 capacity,ScImageInfo *out);
ScStatus ScImageDecodeCancel(ScImageRequest);
#endif
