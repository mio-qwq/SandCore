#ifndef SCCRONTAB_H
#define SCCRONTAB_H
#include "SCCALENDAR.H"
#include "SCLAUNCH.H"
#define CRON_ROWS 32
typedef struct {u32 bits[5][2];int day_any,week_any;char command[1024];} cron_entry;
typedef struct {int count;cron_entry rows[CRON_ROWS];} cron_table;
static int cron_parse(char *text,u32 bytes,cron_table *out);
static int cron_read(const char *path,cron_table *out,u32 *generation);
static int cron_path(const char *directory,char out[64]);
static int cron_matches(const cron_entry *entry,const u32 time[8]);
#endif
