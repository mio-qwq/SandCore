static void bk_rank_if_needed(void)
{
    if(bk_state!=2||bk_score_saved)return;
    int rank=-1;for(int i=0;i<5&&rank<0;i++)if((u32)bk_score>bk_hs_score[i])rank=i;
    if(rank>=0){bk_new_rank=rank;bk_name[0]=0;bk_state=3;}else bk_score_saved=1;
    ui_followup=1;
}
static void bk_reset_lock(void)
{if(bk_lock_resets<15){bk_lock_wait=0;bk_lock_resets++;}}
static void bk_show_about(void)
{if(bk_state==0)bk_state=1;pk_about("SandBlocks","Falling-block puzzle");}
int main(void)
{
    if(ui_open("SandBlocks / SandCore ext")<0)return 1;
    ex_rand_seed((u32)sc_tick());bk_ghost=bk_sound=1;
    char cfg[2048],v[16];if(pk_read_text("HOME/BLOCKS.CFG",cfg,sizeof(cfg))>=0){
        if(!ex_cfg_get(cfg,"ghost",v,16))bk_ghost=!equal(v,"0");
        if(!ex_cfg_get(cfg,"sound",v,16))bk_sound=!equal(v,"0");
        int l;if(!ex_cfg_get(cfg,"startlevel",v,16)&&!ex_parse_int(v,&l))bk_start_level=ex_clamp(l,1,10);}
    if(bk_sound)exaudio_init();bk_scores_load();bk_new_game();int last=sc_tick();
    for(;;){
        if(!ui_frame_due_interval(bk_state||bk_help?0:4))continue;
        ui_pointer();int now=sc_tick(),dt=ex_clamp(now-last,0,20);last=now;
        if(!ui_focus&&bk_state==0){bk_state=1;ui_followup=1;}
        int key=sc_key();
        if(key==1){bk_help=!bk_help;if(bk_help&&bk_state==0)bk_state=1;ui_followup=1;}
        else if(bk_help){if(key==27){bk_help=0;ui_followup=1;}}
        else if(key==27){
            if(bk_state==0){bk_state=1;ui_followup=1;}
            else{exaudio_shutdown();return 0;}
        }else if(bk_state==3){
            int n=length(bk_name);
            if(key==10){if(!n)copy(bk_name,"PLAYER",12);bk_submit_score();}
            else if(key==8){if(n)bk_name[n-1]=0;}
            else if(key>=33&&key<127&&n<11){bk_name[n++]=(char)key;bk_name[n]=0;}
        }else if(key=='r'||key=='R'){
            if(bk_state!=0||ui_confirm("New game?","Current progress will be lost."))bk_new_game();last=sc_tick();dt=0;
        }else if(key=='p'||key=='P'||(key==10&&bk_state==1)){if(bk_state<2){bk_state=!bk_state;dt=0;}}
        else if(bk_state==0){
            if(key==0x82&&bk_fits(bk_px-1,bk_py,bk_piece,bk_rot)){bk_px--;bk_reset_lock();}
            else if(key==0x83&&bk_fits(bk_px+1,bk_py,bk_piece,bk_rot)){bk_px++;bk_reset_lock();}
            else if(key==0x80){if(bk_rotate(1))bk_reset_lock();}
            else if(key=='z'||key=='Z'){if(bk_rotate(-1))bk_reset_lock();}
            else if(key==0x81)bk_drop(0);else if(key==' ')bk_drop(1);
            else if(key=='c'||key=='C')bk_hold_swap();
        }
        /* 输入先处理，再补真实时钟步；暂停/帮助期间从不推进棋盘。 */
        if(!bk_help)for(int i=0;i<dt&&bk_state==0;i++)bk_gravity();
        bk_rank_if_needed();bk_draw();ui_present();
        if(ui_action==PK_ABOUT_ID){bk_show_about();last=sc_tick();continue;}
        if(!bk_help&&ui_action){
            if(bk_state==3&&ui_action==1){if(!bk_name[0])copy(bk_name,"PLAYER",12);bk_submit_score();}
            else if(ui_action==2||(bk_state==2&&ui_action==1))bk_new_game();
            else if(ui_action==1&&bk_state==1){bk_state=0;last=sc_tick();}
        }
        exaudio_pump();
    }
}
