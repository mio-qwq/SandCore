#include "../SCCRONTAB.inc"
static cron_table active,staging;
static u32 jobs[16];static int job_count,jobs_failed;
static void collect(void)
{for(int i=0;i<job_count;i++){u32 state[8];int r=sc_wait2(jobs[i],state);if(r<=0){if(r<0 || state[2]){jobs_failed=1;cli_error("crond: job exited",r<0?r:(int)state[2]);}jobs[i--]=jobs[--job_count];}}}
int main(void)
{
    if(cli_parse()<0)return 2;const char *directory=0;int once=0;
    for(int i=0;i<cli_argc;i++){if(equal(cli_argv[i],"-f"))continue;if(equal(cli_argv[i],"--once")){once=1;continue;}
        if(equal(cli_argv[i],"-c") && i+1<cli_argc){directory=cli_argv[++i];continue;}return 2;}
    char path[64],absolute[64];if(cron_path(directory,path)<0 || cli_path(path,absolute)<0)return 1;
    u32 generation=0,rejected=0,last[6]={0},poll=0;int first=1,table_error=0;
    int event=sc_stream_event(0,1),null=sc_stream_open("",4,0);if(null<0)return 1;
    for(;;){collect();if(event>=0 && sc_stream_event(0,0)!=event)break;u32 now=sc_tick();if(!first && now-poll<100){sc_yield();continue;}poll=now;first=0;
        u32 meta[8];int exists=sc_fsmeta(absolute,meta);if(exists==-1){active.count=0;generation=rejected=0;}
        else if(exists<0){cli_error("crond: table access",exists);table_error=1;if(once)break;}
        else if(meta[7]!=generation && meta[7]!=rejected){u32 fresh=0;int r=cron_read(path,&staging,&fresh);
            if(r<0){rejected=meta[7];table_error=1;cli_error("crond: invalid replacement retained old table",r);if(once)break;}
            else {
                /* 大struct赋值会让宿主编译器生成libc memcpy调用。
                 * 只复制已解析的行和正文，不复制未用槽/大块padding；
                 * 保持无标准C运行库，reload成本随有效配置而非容量。 */
                active.count=staging.count;for(int i=0;i<active.count;i++){cron_entry *to=active.rows+i,*from=staging.rows+i;
                    for(int f=0;f<5;f++)for(int b=0;b<2;b++)to->bits[f][b]=from->bits[f][b];
                    to->day_any=from->day_any;to->week_any=from->week_any;copy(to->command,from->command,sizeof(to->command));}
                generation=fresh;rejected=0;table_error=0;}}
        u32 time[8];if(calendar_read(time)<0){cli_error("crond: RTC",-1);table_error=1;if(once)break;continue;}
        int changed=0;for(int i=1;i<=5;i++)if(time[i]!=last[i])changed=1;if(!changed)continue;for(int i=1;i<=5;i++)last[i]=time[i];
        /* 每次仅执行当前分钟，不补跑时钟跳跃漏过的分钟。配置替换
         * 不重跑本分钟；旧作业由本任务票据拥有，退出不会遗孤。 */
        for(int i=0;i<active.count;i++)if(cron_matches(active.rows+i,time)){
            if(job_count==16){cli_error("crond: concurrent job capacity",-4);table_error=1;continue;}
            int fds[3]={null,1,2};const char *body=active.rows[i].command;int job=sc_spawn_buffer(body,(u32)length(body),fds,"cron");
            if(job<0){cli_error("crond: launch",job);table_error=1;}else jobs[job_count++]=(u32)job;}
        if(once){while(job_count){collect();if(event>=0 && sc_stream_event(0,0)!=event)goto stopped;sc_yield();}break;}sc_yield();
    }
stopped:
    for(int i=0;i<job_count;i++){u32 state[8];if(sc_job_info2(jobs[i],state)>0 && state[3])sc_kill_generation((int)state[3],state[4]);launch_wait(jobs[i]);}
    sc_stream_close(null,0);if(event>=0)sc_stream_event(0,2);return table_error || jobs_failed?1:0;
}
