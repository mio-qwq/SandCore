#include "../SCCALENDAR.H"
static const char *month_names[12]={"January","February","March","April","May","June","July","August","September","October","November","December"};
static int monday;
static int print_month(int year,int month)
{
    cli_text(1,month_names[month-1]);cli_text(1," ");cli_number(1,year);cli_text(1,"\n");cli_text(1,monday?"Mo Tu We Th Fr Sa Su\n":"Su Mo Tu We Th Fr Sa\n");
    int first=(calendar_weekday(year,month,1)+(monday?6:0))%7,days=calendar_days(year,month),column=first;
    for(int i=0;i<first;i++)cli_text(1,"   ");for(int d=1;d<=days;d++){calendar_digits(d,2,' ');if(++column==7){cli_text(1,"\n");column=0;}else if(d<days)cli_text(1," ");}
    if(column)cli_text(1,"\n");return 0;
}
int main(void)
{
    if(cli_parse()<0)return 2;int at=0,whole=0;for(;at<cli_argc;at++){if(equal(cli_argv[at],"-m"))monday=1;else if(equal(cli_argv[at],"-y"))whole=1;else break;}
    int month=0,year=0,n=cli_argc-at;if(n>2)return 2;if(n==2){if(whole || cli_integer(cli_argv[at],&month)<0 || cli_integer(cli_argv[at+1],&year)<0)return 2;}
    else if(n==1){whole=1;if(cli_integer(cli_argv[at],&year)<0)return 2;}else {u32 value[8];if(calendar_read(value)<0)return 1;month=(int)value[2];year=(int)value[1];}
    if(year<1601 || year>9999 || (!whole && (month<1 || month>12)))return 2;
    if(whole)for(int m=1;m<=12;m++){if(m>1)cli_text(1,"\n");print_month(year,m);}else print_month(year,month);return 0;
}
