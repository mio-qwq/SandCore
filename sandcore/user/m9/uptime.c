#include "../SCTEXT.H"
int main(void){u32 ticks=(u32)sc_tick();cli_text(1,"up ");text_unsigned(1,ticks/8640000);cli_text(1,"d ");text_unsigned(1,ticks/360000%24);cli_text(1,"h ");text_unsigned(1,ticks/6000%60);cli_text(1,"m ");text_unsigned(1,ticks/100%60);cli_text(1,"s\n");return 0;}
