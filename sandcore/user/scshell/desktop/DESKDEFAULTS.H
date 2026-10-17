#ifndef SANDCORE_DESKDEFAULTS_H
#define SANDCORE_DESKDEFAULTS_H
/* 由真实493B发布菜单生成；不是按文件名猜用户意图。 */
typedef struct {const char *id,*label,*modern,*legacy,*icon,*category,*types;} DefApp;
typedef struct {const char *old_line,*new_line;} DefMenuLine;
typedef struct {ScU32 line,app;const char *label,*legacy,*icon;} DefImported;
static const DefApp def_apps[]={
    {"sandcore.files","文件","/APPS/FILES2.SCX","/APPS/FILES.SCX","@FILES","系统","directory"},
    {"sandcore.studio","开发工作室","/APPS/STUDIO2.SCX","/APPS/IDE.SCX","@STUDIO","开发","c h inc asm scw"},
    {"sandcore.debug","调试器","/APPS/DEBUG2.SCX","/APPS/DEBUGGER.SCX","@DEBUG","开发",""},
    {"sandcore.settings","设置","/APPS/SETTINGS2.SCX","/APPS/SETTINGS.SCX","@SETTINGS","系统",""},
    {"sandcore.notes","笔记","/APPS/NOTES2.SCX","/APPS/NOTE.SCX","@NOTE","系统","txt cfg log json md ini"},
    {"sandcore.canvas","画板","/APPS/CANVAS2.SCX","/APPS/CANVAS.SCX","@CANVAS","系统",""},
    {"sandcore.lens","图片","/APPS/LENS2.SCX","/APPS/LENS.SCX","@LENS","系统","scb bmp png jpg jpeg webp"},
    {"sandcore.monitor","系统监视器","/APPS/MONITOR2.SCX","/APPS/MONITOR.SCX","@MONITOR","系统",""},
    {"sandcore.calculator","计算器","/APPS/CALC2.SCX","/APPS/CALC.SCX","@CALC","系统",""},
    {"sandcore.terminal","终端","/APPS/TERMINAL2.SCX","/BIN/SH.SCX","@SHELL","系统",""},
    {"sandcore.palette","调色板","/APPS/PALETTE2.SCX","/APPS/PALETTE.SCX","@PALETTE","系统",""},
    {"sandcore.sound","音频","/APPS/SOUND2.SCX","/APPS/SOUND.SCX","@SOUND","系统","mp3 wav flac"},
    {"sandcore.session","登录与会话","/APPS/SESSION2.SCX","/APPS/SESSION.SCX","@SESSION","系统",""},
    {"sandcore.installer","文件安装","/APPS/INSTALLER2.SCX","","@FILES","系统",""},
};
static const DefMenuLine def_menu_lines[]={
    {"SMENU1MIO\n","SMENU1MIO\n"},
    {"Files|apps/files.scx|@FILES\n","Files|/APPS/FILES2.SCX|@FILES\n"},
    {"Studio|apps/ide.scx|@STUDIO\n","Studio|/APPS/STUDIO2.SCX|@STUDIO\n"},
    {"Debug|apps/debugger.scx|@DEBUG\n","Debug|/APPS/DEBUG2.SCX|@DEBUG\n"},
    {"Settings|apps/settings.scx|@SETTINGS\n","Settings|/APPS/SETTINGS2.SCX|@SETTINGS\n"},
    {"Lens|apps/lens.scx|@LENS\n","Lens|/APPS/LENS2.SCX|@LENS\n"},
    {"Canvas|apps/canvas.scx|@CANVAS\n","Canvas|/APPS/CANVAS2.SCX|@CANVAS\n"},
    {"Monitor|apps/monitor.scx|@MONITOR\n","Monitor|/APPS/MONITOR2.SCX|@MONITOR\n"},
    {"Shell|bin/shell.scx|@SHELL\n","Shell|/APPS/TERMINAL2.SCX|@SHELL\n"},
    {"Lumen|apps/lumen.scx|@LUMEN\n","Lumen|apps/lumen.scx|@LUMEN\n"},
    {"Race|apps/race.scx|@RACE\n","Race|apps/race.scx|@RACE\n"},
    {"World|apps/world.scx|@WORLD\n","World|apps/world.scx|@WORLD\n"},
    {"Notes|apps/note.scx|@NOTE\n","Notes|/APPS/NOTES2.SCX|@NOTE\n"},
    {"Palette|apps/palette.scx|@PALETTE\n","Palette|/APPS/PALETTE2.SCX|@PALETTE\n"},
    {"Calculator|apps/calc.scx|@CALC\n","Calculator|/APPS/CALC2.SCX|@CALC\n"},
    {"Mines|apps/mines.scx|@MINES\n","Mines|apps/mines.scx|@MINES\n"},
    {"Legacy tools|apps/files.scx LEGACY|@FILES\n","Legacy tools|apps/files.scx LEGACY|@FILES\n"},
};
static const DefImported def_imported[]={
    {1,0,"Files","/apps/files.scx","@FILES"},
    {2,1,"Studio","/apps/ide.scx","@STUDIO"},
    {3,2,"Debug","/apps/debugger.scx","@DEBUG"},
    {4,3,"Settings","/apps/settings.scx","@SETTINGS"},
    {5,6,"Lens","/apps/lens.scx","@LENS"},
    {6,5,"Canvas","/apps/canvas.scx","@CANVAS"},
    {7,7,"Monitor","/apps/monitor.scx","@MONITOR"},
    {8,9,"Shell","/bin/shell.scx","@SHELL"},
    {12,4,"Notes","/apps/note.scx","@NOTE"},
    {13,10,"Palette","/apps/palette.scx","@PALETTE"},
    {14,8,"Calculator","/apps/calc.scx","@CALC"},
};
enum {DEF_APP_COUNT=sizeof(def_apps)/sizeof(def_apps[0]),DEF_MENU_BYTES=521};
#endif
