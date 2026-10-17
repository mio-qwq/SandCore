#include "FILES2.H"
#include "files2_model.inc"
#include "files2_storage.inc"
#include "files2_actions.inc"
#include "files2_view.inc"

int main(void)
{
    ScStatus status=f2_create();
    if(status<0){fprintf(stderr,"Files2 startup error=%d\n",status);f2_close();return -status;}
    while(!f2.quit){
        ScU32 ready=0;
        status=ScUiContextStep(f2.ui,0);if(status<0)break;
        ScStatus step=f2_background(&ready);
        if(step<0)f2_error(step,f2.current?f2.current->path:"");
        if(f2.quit)break;
        /* 每批主动让出一次；只有候选/排序/已就绪消息还可推进时继续。
         * 等待与输入队列检查由Broker原子完成，避免有键睡眠和空轮询。 */
        if(ready){ScProcessYield();continue;}
        ScBrokerWaitDesc wait;memset(&wait,0,sizeof(wait));wait.size=sizeof(wait);wait.version=1;
        wait.event_mask=SC32_EVENT_INPUT|SC32_EVENT_VISIBILITY|SC32_EVENT_STREAM|SC32_EVENT_JOB;
        wait.timeout=100;
        status=ScUiContextWaitTimeout(f2.ui,100,&wait.timeout);if(status<0)break;
        ScU32 events=0;status=ScBrokerWait(&wait,&events);if(status<0)break;
    }
    if(status<0){
        ScUiErrorInfo detail;memset(&detail,0,sizeof(detail));detail.size=sizeof(detail);detail.version=1;
        /* 终止必须保留库的原始原因；只有聚合码313无法分辨输入、计时器
         * 或绘制失败，更不能据此吞掉所有负值继续运行。 */
        if(ScUiErrorQuery(f2.ui,&detail)==0)
            fprintf(stderr,"Files2 stopped error=%d cause=%d raw=%d operation=%u node=%u\n",
                status,detail.cause_code,detail.raw,detail.operation,detail.node);
        else fprintf(stderr,"Files2 stopped error=%d\n",status);
    }
    f2_close();return status<0?-status:0;
}
