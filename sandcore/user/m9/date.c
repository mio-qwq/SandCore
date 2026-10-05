#include "../SCCALENDAR.H"
static const char *weekdays[7]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char *months[12]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
static u32 clock_value[8];
static int format(const char *p,int depth)
{
    if(depth>4)return -1;int y=(int)clock_value[1],m=(int)clock_value[2],d=(int)clock_value[3],h=(int)clock_value[4],w=calendar_weekday(y,m,d);
    while(*p){char c=*p++;if(c!='%'){if(cli_write(1,&c,1)<0)return -1;continue;}c=*p++;if(!c)return -1;
        if(c=='Y')calendar_digits(y,4,'0');else if(c=='y')calendar_digits(y%100,2,'0');else if(c=='m')calendar_digits(m,2,'0');else if(c=='d'||c=='e')calendar_digits(d,2,c=='e'?' ':'0');
        else if(c=='H')calendar_digits(h,2,'0');else if(c=='I')calendar_digits(h%12?h%12:12,2,'0');else if(c=='M')calendar_digits((int)clock_value[5],2,'0');else if(c=='S')calendar_digits((int)clock_value[6],2,'0');
        else if(c=='a')cli_text(1,weekdays[w]);else if(c=='b'||c=='h')cli_text(1,months[m-1]);else if(c=='p')cli_text(1,h>=12?"PM":"AM");else if(c=='Z')cli_text(1,"UTC");else if(c=='z')cli_text(1,"+0000");
        else if(c=='F'){if(format("%Y-%m-%d",depth+1)<0)return -1;}else if(c=='T'){if(format("%H:%M:%S",depth+1)<0)return -1;}else if(c=='R'){if(format("%H:%M",depth+1)<0)return -1;}
        else if(c=='u')calendar_digits(w?w:7,1,'0');else if(c=='w')calendar_digits(w,1,'0');else if(c=='j'){int day=d;for(int i=1;i<m;i++)day+=calendar_days(y,i);calendar_digits(day,3,'0');}
        else if(c=='n')cli_text(1,"\n");else if(c=='t')cli_text(1,"\t");else if(c=='%')cli_text(1,"%");else return -1;}
    return 0;
}
int main(void)
{if(cli_parse()<0)return 2;const char *text="%a %b %e %T UTC %Y";int custom=0;
    for(int i=0;i<cli_argc;i++){const char *p=cli_argv[i];if(equal(p,"-u")){}else if(equal(p,"-R"))text="%a, %d %b %Y %T +0000";
        else if(equal(p,"-I"))text="%F";else if(equal(p,"-Iseconds"))text="%FT%T+00:00";else if(p[0]=='+' && !custom){text=p+1;custom=1;}else return 2;}
    if(calendar_read(clock_value)<0)return cli_error("date: RTC",-1);if(format(text,0)<0)return 2;return cli_text(1,"\n")<0;
}
