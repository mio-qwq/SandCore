/* =====================================================================
 * mio：DUNE RUN内容层。用真实累计里程计算五名对手、超车和终点次序。
 * 三条路线的长度/曲率/环境不同，三站锦标赛对同一批车手累计积分。
 * 计时赛没有交通；自由跑不设终点；legacy恢复12000单位原圈长。
 * 所有倒计时、损伤、氮气及维修按固定4tick仿真推进，不依赖帧数。
 * SCREC2MIO仅记录完成的圈/赛事，退出中途不伪造一个完成成绩。
 * ===================================================================== */
static const char *race_tracks[3]={"Dune Circuit","Coastal Run","Highland Pass"};
static const char *race_names[6]={"YOU","Sable","Tide","Flint","Ember","Cobalt"};
static int track_lengths[3]={60000,78000,96000};
static int rival_distance[RACE_RIVALS],rival_speed[RACE_RIVALS],rival_lane[RACE_RIVALS];
static int rival_finish[RACE_RIVALS],race_finished,race_elapsed,race_place=1;
static int race_points,race_round,race_finished_time,race_best_laps[3],race_best_times[3];
static int race_scores[6],race_record_error,race_contact_until,race_recent_lap;
static int race_countdown=300,race_boost=1000,race_condition=1000,race_drift,race_boosting;
static int race_menu_page,race_difficulty=1,race_setup=1,race_pit,race_medal;
static int race_choice_track,race_choice_mode=2,race_choice_difficulty=1,race_result_page;
static u8 race_record[64];
static int race_length(void){return race_legacy?12000:track_lengths[race_track];}
static int race_has_rivals(void){return race_legacy || race_mode!=1;}
static void race_record32(int offset,u32 value)
{for(int i=0;i<4;i++){race_record[offset+i]=(u8)value;value>>=8;}}
static u32 race_record_value(int offset)
{return (u32)race_record[offset]|(u32)race_record[offset+1]<<8|(u32)race_record[offset+2]<<16|(u32)race_record[offset+3]<<24;}
static u32 race_record_hash(void)
{u32 value=2166136261u;for(int i=16;i<64;i++)value=(value^race_record[i])*16777619u;return value;}
static void race_records_save(void)
{
    for(int i=0;i<64;i++)race_record[i]=0;
    const char *magic="SCREC2MIO";for(int i=0;i<9;i++)race_record[i]=(u8)magic[i];
    race_record32(16,2);
    for(int i=0;i<3;i++){race_record32(20+i*4,race_best_laps[i]);race_record32(32+i*4,race_best_times[i]);}
    race_record32(44,race_difficulty);race_record32(48,race_medal);
    race_record32(12,race_record_hash());
    race_record_error=sc_write("HOME/RACE/CAREER.REC",race_record,64)!=64;
}
static void race_records_load(void)
{
    u32 info[2];
    if(sc_stat("HOME/RACE/CAREER.REC",info)||info[0]!=1||info[1]!=64
        ||sc_read("HOME/RACE/CAREER.REC",race_record,64)!=64)return;
    const char *magic="SCREC2MIO";for(int i=0;i<9;i++)if(race_record[i]!=(u8)magic[i])return;
    if(race_record_value(16)!=2||race_record_value(12)!=race_record_hash())return;
    for(int i=0;i<3;i++)if(race_record_value(20+i*4)>8640000||race_record_value(32+i*4)>8640000)return;
    if(race_record_value(44)>2||race_record_value(48)>3)return;
    for(int i=9;i<12;i++)if(race_record[i])return;
    for(int i=52;i<64;i++)if(race_record[i])return;
    for(int i=0;i<3;i++){race_best_laps[i]=(int)race_record_value(20+i*4);race_best_times[i]=(int)race_record_value(32+i*4);}
    race_choice_difficulty=race_difficulty=(int)race_record_value(44);race_medal=(int)race_record_value(48);
}
static void race_restart(void)
{
    race_finished=race_elapsed=race_finished_time=race_contact_until=race_recent_lap=0;race_place=1;
    race_countdown=race_legacy?0:300;race_condition=race_boost=1000;race_drift=race_boosting=race_pit=0;
    drive_mouse=0;race_result_page=0;race_place=race_has_rivals()?6:1;
    for(int i=0;i<RACE_RIVALS;i++){
        rival_distance[i]=400+i*460;rival_speed[i]=65+i*3+race_difficulty*7;
        rival_lane[i]=(i-2)*30;rival_finish[i]=0;
    }
    if(!race_legacy)best=race_best_laps[race_track];
    copy(race_message,"N boost / Space drift / M pause",sizeof(race_message));
}
static void race_rivals_step(int before)
{
    race_elapsed+=4;
    /* 无尽模式定期同时减去完整圈数，保持相对里程/曲率相位。
     * 不能数小时后signed里程溢出把对手放到玩家身后。 */
    if(!race_mode && lap>1000){
        int offset=500*race_length();lap-=500;before-=offset;
        for(int i=0;i<RACE_RIVALS;i++)rival_distance[i]-=offset;
    }
    if(!race_mode && race_elapsed>1000000000){
        race_elapsed-=500000000;lap_start-=500000000;race_contact_until-=500000000;
    }
    int total=lap*race_length()+distance;race_place=1;
    if(!race_has_rivals())return;
    if(!race_mode)for(int i=0;i<RACE_RIVALS;i++)
        if(rival_distance[i]<total-2*race_length() || rival_distance[i]>total+2*race_length())
            rival_distance[i]=total+1400+i*520; /* 自由跑补充离开视野的交通车 */
    for(int i=0;i<RACE_RIVALS;i++){
        if(!rival_finish[i]){
            int pace=rival_speed[i]-ui_abs(curvature(rival_distance[i]))/6;
            int old=rival_distance[i];rival_distance[i]+=ui_clamp(pace,38,108);
            if(race_mode && rival_distance[i]>=3*race_length()){
                rival_distance[i]=3*race_length();rival_finish[i]=race_elapsed;
            }
            /* 超越检测检查前后两个相对位置；从前到后或从后到前
             * 都可碰撞，不会高速穿车。车道避让也是真实状态变化。
             * 一次接触冷却60tick，避免同一次重叠扣几十次耐久。 */
            int lane=(i-2)*30+scn_sin(race_elapsed/2+i*177)*12/256;
            if(ui_abs(old-before)<500 && ui_abs(lane-steer)<28)lane+=i&1?34:-34;
            rival_lane[i]+=ui_clamp(lane-rival_lane[i],-2,2);
            int rb=old-before,ra=rival_distance[i]-total;
            int crossed=(rb>0&&ra<=0)||(rb<0&&ra>=0);
            if((ui_abs(ra)<105||crossed) && ui_abs(steer-rival_lane[i])<22
                && race_elapsed>=race_contact_until){
                speed=speed*2/5;crashes++;race_condition=ui_clamp(race_condition-110,180,1000);
                race_contact_until=race_elapsed+60;copy(race_message,"Contact / bodywork damaged",sizeof(race_message));
            }
        }
        if(rival_distance[i]>total || (rival_finish[i] && rival_distance[i]>=total))race_place++;
    }
}
static void race_complete_lap(void)
{
    race_recent_lap=lap_time;
    if(!race_legacy && (!race_best_laps[race_track]||lap_time<race_best_laps[race_track])){
        race_best_laps[race_track]=best=lap_time;race_records_save();
    }
    if(!race_mode || lap<3)return;
    race_finished=1;speed=0;drive_mouse=0;race_finished_time=race_elapsed;
    if(!race_best_times[race_track]||race_elapsed<race_best_times[race_track])race_best_times[race_track]=race_elapsed;
    /* 尚未到终点的对手按其真实剩余里程排名；已完成者按完成tick。
     * 玩家插入本场实际名次。积分用相同规则分配给全部六名车手，
     * 最后一站按总积分决出冠军，不能用最后一站名次代替锦标赛。 */
    if(race_mode==2){
        race_scores[0]+=7-race_place;
        for(int i=0;i<RACE_RIVALS;i++){
            int rank=1;
            for(int j=0;j<RACE_RIVALS;j++)if(j!=i){
                int ahead=rival_finish[j]?(rival_finish[i]?rival_finish[j]<rival_finish[i]:1):
                    (!rival_finish[i] && rival_distance[j]>rival_distance[i]);
                if(ahead || (rival_distance[j]==rival_distance[i] && rival_finish[j]==rival_finish[i] && j<i))rank++;
            }
            if(!rival_finish[i] || rival_finish[i]>race_elapsed)rank++;
            race_scores[i+1]+=7-rank;
        }
        race_points=race_scores[0];
        if(race_round==2){
            int rank=1;for(int i=1;i<6;i++)if(race_scores[i]>race_scores[0])rank++;
            int medal=rank==1?3:rank==2?2:rank==3?1:0;if(medal>race_medal)race_medal=medal;
        }
    }
    race_records_save();copy(race_message,race_record_error?"Finished / save failed":"Finished / records saved",sizeof(race_message));
}
/* 六个积分槽按身份固定，展示顺序另行投影：绝不能为了排序HUD
 * 交换race_scores，下一站又把Sable的得分加进YOU槽。同分使用
 * 固定车手顺序作公开稳定规则；每名车手始终占据唯一的表格名次。 */
static int race_driver_at(int place)
{
    for(int driver=0;driver<6;driver++){
        int rank=0;
        for(int other=0;other<6;other++)
            if(race_scores[other]>race_scores[driver]
                || (race_scores[other]==race_scores[driver] && other<driver))rank++;
        if(rank==place)return driver;
    }
    return 0;
}
/* 内容菜单按页展示，每页最多三行。在VGA或200%缩放的小客户区
 * 中仍保留下一页/开始/返回按钮，不让键盘成为小屏唯一操作方式。 */
static void race_menu_draw(void)
{
    ui_header("DUNE RUN",race_setup?"Select your next race":"PAUSED / simulation stopped");
    int width=ui_clamp(UI_W-24,200,540),x=(UI_W-width)/2,y=ui_compact?34:66;
    if(race_menu_page==0){
        for(int i=0;i<3;i++)ui_small_control(100+i,x,y+i*26,width,race_tracks[i],race_choice_track==i);
    }else if(race_menu_page==1){
        ui_small_control(110,x,y,width,"Time trial / 3 laps",race_choice_mode==1);
        ui_small_control(111,x,y+26,width,"Championship / 3 races",race_choice_mode==2);
        ui_small_control(112,x,y+52,width,"Free drive / endless",race_choice_mode==0);
    }else{
        const char *names[3]={"Touring / relaxed rivals","Sport / balanced rivals","Expert / fast rivals"};
        for(int i=0;i<3;i++)ui_small_control(130+i,x,y+i*26,width,names[i],race_choice_difficulty==i);
    }
    if(UI_H>330){
        ui_text(x,y+94,"WASD / mouse pedals",PAL_UI_TEXT);
        ui_text(x,y+116,"N: boost / Space: drift",PAL_UI_MUTED);
        ui_text(x,y+138,"Pit: slow on right at start",PAL_UI_MUTED);
        ui_text(x,y+160,"M: pause / R: restart",PAL_UI_MUTED);
    }
    int row=UI_H-52;
    ui_small_control(114,x,row,54,"Page",0);
    ui_small_control(113,x+60,row,width-120,race_setup?"Start":"Restart",0);
    ui_small_control(115,x+width-54,row,54,race_setup?"Quit":"Back",0);
    ui_footer(race_menu_page==0?"CIRCUIT / 1-3":race_menu_page==1?"EVENT / T C F":"DIFFICULTY");
}
static void race_content_draw(void)
{
    char text[96],number[16];
    if(!race_finished){
        if(UI_H<240){
            copy(text,"Lap ",sizeof(text));decimal(number,lap+1);append(text,number,sizeof(text));
            append(text," / P",sizeof(text));decimal(number,race_place);append(text,number,sizeof(text));
            append(text," / B",sizeof(text));decimal(number,race_boost/10);append(text,number,sizeof(text));
            append(text," / C",sizeof(text));decimal(number,race_condition/10);append(text,number,sizeof(text));
            ui_text(12,UI_H-73,text,PAL_UI_TEXT);
            if(race_countdown){
                ui_panel(UI_W/2-20,28,40,24);ui_number(UI_W/2-4,31,(race_countdown+99)/100,PAL_UI_CYAN+7);
            }
            return;
        }
        ui_panel(12,ui_compact?60:82,UI_W>420?270:UI_W-24,ui_compact?46:70);
        copy(text,race_tracks[race_track],sizeof(text));append(text," / ",sizeof(text));
        if(race_has_rivals()){decimal(number,race_place);append(text,number,sizeof(text));append(text," of 6",sizeof(text));}
        else append(text,"TIME TRIAL",sizeof(text));
        ui_text(20,ui_compact?64:88,text,PAL_UI_TEXT);
        copy(text,"Boost ",sizeof(text));decimal(number,race_boost/10);append(text,number,sizeof(text));
        append(text," / Car ",sizeof(text));decimal(number,race_condition/10);append(text,number,sizeof(text));
        ui_text(20,ui_compact?84:110,text,PAL_UI_MUTED);
        if(!ui_compact)ui_text(20,130,race_pit?"PIT / REPAIRING":race_boosting?"BOOST":race_drift?"CONTROLLED DRIFT":"KEEP YOUR LINE",PAL_UI_CYAN+7);
        if(race_countdown){
            ui_panel(UI_W/2-44,UI_H/2-30,88,56);ui_number(UI_W/2-5,UI_H/2-15,(race_countdown+99)/100,PAL_UI_TEXT);
        }
        return;
    }
    int width=ui_clamp(UI_W-24,240,500),height=ui_clamp(UI_H-28,140,340),x=(UI_W-width)/2,y=(UI_H-height)/2;
    /* 结果层独占当前帧按钮；底部油门按键不能透过结束弹窗生效。 */
    ui_action=0;ui_panel(x,y,width,height);
    ui_text(x+12,y+10,race_mode==2&&race_round==2?"SERIES COMPLETE":"FINISH",PAL_UI_TEXT);
    copy(text,"Place ",sizeof(text));decimal(number,race_place);append(text,number,sizeof(text));
    append(text," / Time ",sizeof(text));decimal(number,race_finished_time/100);append(text,number,sizeof(text));append(text,"s",sizeof(text));
    if(race_mode==2&&race_round==2){
        int overall=1;for(int i=1;i<6;i++)if(race_scores[i]>race_scores[0])overall++;
        copy(text,"Overall ",sizeof(text));decimal(number,overall);append(text,number,sizeof(text));
        append(text," / points ",sizeof(text));decimal(number,race_scores[0]);append(text,number,sizeof(text));
    }
    /* 标题、摘要、成绩表、存盘状态、按钮各有自己的纵向空间。
     * VGA的140逻辑像素结果卡放不下独立表格，因此Table轮换
     * 摘要/六名车手，而不是强行在摘要与保存提示中间塞一行。
     * 高卡片保留摘要，在其下分页画表；两种布局都可鼠标看全。 */
    int compact_table=race_mode==2 && height<160 && race_result_page%7;
    if(compact_table){
        int rank=race_result_page%7-1,driver=race_driver_at(rank);
        decimal(text,rank+1);append(text,"  ",sizeof(text));
        append(text,race_names[driver],sizeof(text));append(text," / ",sizeof(text));
        decimal(number,race_scores[driver]);append(text,number,sizeof(text));
    }
    ui_text(x+12,y+34,text,PAL_UI_CYAN+7);
    if(race_mode==2 && height>=160){
        int rows=(height-134)/22;if(rows<1)rows=1;
        int first=(race_result_page%((6+rows-1)/rows))*rows;
        for(int i=first;i<6&&i<first+rows;i++){
            int driver=race_driver_at(i);
            decimal(text,i+1);append(text,"  ",sizeof(text));append(text,race_names[driver],sizeof(text));
            append(text,"  ",sizeof(text));decimal(number,race_scores[driver]);append(text,number,sizeof(text));
            ui_text(x+12,y+58+(i-first)*22,text,driver?PAL_UI_MUTED:PAL_UI_TEXT);
        }
    }
    if(race_mode==2)ui_small_control(122,x+width-68,y+8,56,"Table",0);
    ui_text(x+12,y+height-76,race_record_error?"SAVE FAILED / retry with S":"Record saved",PAL_UI_MUTED);
    ui_small_control(120,x+12,y+height-52,(width-30)/2,race_mode==2&&race_round<2?"Next event":"Replay",0);
    ui_small_control(121,x+18+(width-30)/2,y+height-52,(width-30)/2,"Selection",0);
}
static void race_start_event(void)
{race_menu=race_setup=0;reset();}
static void race_new_series(void)
{
    race_track=race_choice_track;race_mode=race_choice_mode;race_difficulty=race_choice_difficulty;
    race_points=race_round=0;for(int i=0;i<6;i++)race_scores[i]=0;
    if(race_mode==2)race_track=0;race_start_event();
}
static void race_advance(void)
{
    if(race_mode==2 && race_round<2){race_round++;race_track=race_round;race_start_event();}
    else race_new_series();
}
static void race_content_key(int key)
{
    if(key=='m'||key=='M'){
        if(!race_menu){race_choice_track=race_track;race_choice_mode=race_mode;race_choice_difficulty=race_difficulty;}
        race_menu=!race_menu;drive_mouse=0;return;
    }
    if(race_menu){
        if(key>='1'&&key<='3')race_choice_track=key-'1';
        if(key=='t'||key=='T')race_choice_mode=1;if(key=='c'||key=='C')race_choice_mode=2;if(key=='f'||key=='F')race_choice_mode=0;
        if(key==9)race_menu_page=(race_menu_page+1)%3;
        if(key==10)race_new_series();
    }else if(race_finished && key==10)race_advance();
    if(race_finished&&(key=='s'||key=='S'))race_records_save();
}
static void race_content_action(int action)
{
    if(action>=100&&action<=102)race_choice_track=action-100;
    if(action==110)race_choice_mode=1;if(action==111)race_choice_mode=2;if(action==112)race_choice_mode=0;
    if(action>=130&&action<=132)race_choice_difficulty=action-130;
    if(action==113)race_new_series();if(action==114)race_menu_page=(race_menu_page+1)%3;
    if(action==115){if(race_setup)sc_exit(0);race_menu=0;drive_mouse=0;}
    if(action==116 && !race_menu && !race_finished)race_content_key('m');
    if(action==120)race_advance();if(action==121){race_menu=race_setup=1;race_choice_track=race_track;race_choice_mode=race_mode;race_choice_difficulty=race_difficulty;}
    if(action==122)race_result_page++;
}
