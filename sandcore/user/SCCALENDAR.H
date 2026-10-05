#ifndef SCCALENDAR_H
#define SCCALENDAR_H
#include "SCTEXT.H"
static int calendar_leap(int year){return !(year%4) && (year%100 || !(year%400));}
static int calendar_days(int year,int month)
{static const int days[12]={31,28,31,30,31,30,31,31,30,31,30,31};return days[month-1]+(month==2 && calendar_leap(year));}
static int calendar_weekday(int year,int month,int day)
{static const int offsets[12]={0,3,2,5,0,3,5,1,4,6,2,4};if(month<3)year--;return (year+year/4-year/100+year/400+offsets[month-1]+day)%7;}
static void calendar_digits(int value,int width,char pad)
{char text[12];decimal(text,value);int n=length(text);while(n<width){cli_write(1,&pad,1);n++;}cli_text(1,text);}
static int calendar_read(u32 out[8])
{int r=-6;for(int tries=0;tries<4 && r==-6;tries++){r=sc_clock2(out);if(r==-6)sc_yield();}return r;}
#endif
