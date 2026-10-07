/* 会话控制是普通三环界面：密码只交内核派生，窗口不保存凭据。
 * 固定LOGIN.SCX也复用此源，文件主体被内核限定为无权限身份。 */
#include "SCAPI.H"
#include "NUI.inc"
static char username[32],password[129],message[96]="Each login has its own desktop";
static int field,ticket,mode,rows;
static u32 sessions[272],cursor=0xFFFFFFFFu,next_cursor=0xFFFFFFFFu;
static u32 login_started;
static void wipe(void *p,u32 size){volatile u8 *out=p;while(size--)*out++=0;}
static void result(int value,const char *ok)
{if(value>=0)copy(message,ok,sizeof(message));else {copy(message,"Operation failed: ",sizeof(message));char n[12];decimal(n,value);append(message,n,sizeof(message));}}
static void login(void)
{
    if(ticket || !username[0])return;
    int r=sc_auth_login(username,password);wipe(password,sizeof(password));
    if(r>0){ticket=r;login_started=(u32)sc_tick();copy(message,"Verifying password...",sizeof(message));}
    else result(r,"Login");ui_followup=1;
}
static void draw(void)
{
    ui_header("LOGIN / DESKTOPS","F12 opens this panel; background tasks keep running");
    const int tabs[]={1,2};const char *labels[]={"Login","My sessions"};int y=ui_toolbar(tabs,labels,2,ui_compact?45:70);
    if(mode==0){
        int width=UI_W-48;if(width>520)width=520;if(width<120)width=120;
        ui_text(24,y,"USERNAME",PAL_UI_CYAN+7);y+=22;
        ui_panel(24,y,width,30);ui_text(32,y+7,username,PAL_UI_TEXT);
        if(field==0)ui_rect(30,y+27,width-12,1,PAL_UI_CYAN+7);
        if((ui_pressed&1) && ui_hit(24,y,width,30))field=0;y+=42;
        ui_text(24,y,"PASSWORD",PAL_UI_CYAN+7);y+=22;
        char hidden[129];int n=length(password);for(int i=0;i<n;i++)hidden[i]='*';hidden[n]=0;
        ui_panel(24,y,width,30);ui_text(32,y+7,hidden,PAL_UI_TEXT);
        if(field==1)ui_rect(30,y+27,width-12,1,PAL_UI_CYAN+7);
        if((ui_pressed&1) && ui_hit(24,y,width,30))field=1;y+=44;
        ui_control(3,24,y,116,ticket?"Verifying":"Sign in",ticket!=0);
        ui_control(4,150,y,112,"Clear",0);
    }else{
        rows=(UI_H-y-70)/42;if(rows<1)rows=1;if(rows>16)rows=16;
        int r=sc_session_page(sessions,(u32)(16+rows*16),cursor);
        if(r<0){result(r,"Sessions");rows=0;}else {rows=r;next_cursor=sessions[4];}
        if(!rows)ui_text(24,y,"No accessible normal sessions",PAL_UI_MUTED);
        for(int i=0;i<rows;i++){u32 *row=sessions+16+i*16;int yy=y+i*42;
            ui_panel(20,yy,UI_W-40,36);ui_clip_set(30,yy+5,260,26);ui_text(30,yy+9,(char *)(row+8),PAL_UI_TEXT);ui_clip_clear();
            if(UI_W>820){ui_text(320,yy+9,"ID",PAL_UI_MUTED);ui_number(348,yy+9,(int)row[0],PAL_UI_MUTED);}
            ui_control(100+i,UI_W-240,yy+3,100,row[4]?"Active":"Switch",row[4]!=0);
            ui_control(200+i,UI_W-132,yy+3,100,"Log out",0);
        }
        y+=rows*42+8;ui_control(5,24,y,100,"First",0);ui_control(6,132,y,100,"Next",0);
    }
    ui_footer(message);
}
int main(void)
{
    if(ui_open("Login / sessions")<0)return 1;
    for(;;){
        /* 派生状态是后台工作，可在界面不可见时完成；绘制仍只在
         * 可见性合同允许后进行，不用暂停整会话实现隐藏。 */
        if(ticket){u32 status[8];int r=sc_auth_status((u32)ticket,status);
            if(r!=1){int old=ticket;ticket=0;if(r>=0){r=sc_auth_exec((u32)old,"",1);result(r,"Logged in");}
                else result(r,"Login");sc_auth_cancel((u32)old);ui_followup=1;}
            else if((u32)sc_tick()-login_started>60000u){sc_auth_cancel((u32)ticket);ticket=0;copy(message,"Login expired",sizeof(message));ui_followup=1;}}
        if(!ui_frame_due())continue;ui_pointer();draw();ui_present();int action=ui_action;
        if(action==1 || action==2){mode=action-1;ui_followup=1;}
        else if(action==3)login();
        else if(action==4){wipe(password,sizeof(password));username[0]=0;ui_followup=1;}
        else if(action==5){cursor=0xFFFFFFFFu;ui_followup=1;}
        else if(action==6 && next_cursor!=0xFFFFFFFFu){cursor=next_cursor;ui_followup=1;}
        else if(action>=100 && action<100+rows){result(sc_session_control(sessions[16+(action-100)*16],0),"Desktop switched");ui_followup=1;}
        else if(action>=200 && action<200+rows){u32 id=sessions[16+(action-200)*16];
            if(ui_confirm("Log out this session?","Its processes and unsaved windows will close"))result(sc_session_control(id,1),"Session logged out");
            cursor=0xFFFFFFFFu;ui_followup=1;}
        int key=sc_key();if(key<0)continue;
        if(key==27){if(ticket)sc_auth_cancel((u32)ticket);wipe(password,sizeof(password));return 0;}
        if(key==9){field=!field;ui_followup=1;continue;}
        if(mode || ticket)continue;
        if(key==10){login();continue;}
        char *text=field?password:username;int n=length(text),capacity=field?129:32;
        if(key==8 && n)text[n-1]=0;else if(key>=32 && key<127 && n+1<capacity){text[n]=(char)key;text[n+1]=0;}
        ui_followup=1;
    }
}
