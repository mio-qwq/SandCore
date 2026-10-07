#include "../../kernel/module.h"

/* 仅签名验收夹具：编号由构建参数决定，入口/全局指针/回调均需要真实
 * 重定位。代理只构建待签字节；用户检查源码并独立签署，没有开发公钥。
 * 日志上限只限制本夹具的观察长度，不影响系统模块或任务数量。 */
#ifndef FIXTURE_NUMBER
#error FIXTURE_NUMBER is required
#endif
static const module_api_t *services;
static u32 initialized,irq_count,service_count;
static void *owned_memory;

static int append_order(void)
{
    u32 log[63];
    int bytes=services->read("/TMP/M10ORDER",log,sizeof(log),0);
    if(bytes<0)bytes=0;
    if(bytes%12 || bytes>(int)sizeof(log)-12)return 11;
    u32 at=(u32)bytes/4;
    log[at]=FIXTURE_NUMBER;log[at+1]=initialized;log[at+2]=services->ticks();
    return services->write("/TMP/M10ORDER",(const u8 *)log,(u32)bytes+12)==bytes+12?0:12;
}

#ifdef FIXTURE_CALLBACKS
static void on_irq(u32 line,void *context)
{
    /* 共享PIT只计数，不清其它设备状态、不写盘、不分配或执行重活。
     * EOI仍归内核公共IRQ出口，失败入口的注册项绝不能被调用。 */
    if(line==0 && context==owned_memory)irq_count++;
}
static void on_service(void *context)
{
    if(context!=owned_memory)return;
    service_count++;
    u32 record[6]={1,FIXTURE_NUMBER,initialized,irq_count,service_count,services->ticks()};
#ifdef FIXTURE_FAIL
    const char *path="/TMP/M10FAILED";
#else
    const char *path="/TMP/M10SERVICE";
#endif
    (void)services->write(path,(const u8 *)record,sizeof(record));
}
#endif

int module_init(const module_api_t *api)
{
    if(!api || api->version!=1 || api->size<92)return 9;
    services=api;initialized++;
    int result=append_order();if(result)return result;
#ifdef FIXTURE_CALLBACKS
    owned_memory=api->allocate(8192);if(!owned_memory)return 8;
    if(api->irq_register(0,on_irq,owned_memory)<0
        || api->service_register(on_service,owned_memory,100)<0)return 10;
#ifdef FIXTURE_FAIL
    /* 故意不释放，让真实失败清理撤掉两个注册项与辅助页。
     * 若回调错误地留下，M10FAILED文件会成为外部可观察的反证。 */
    return 7;
#endif
#endif
    return 0;
}
