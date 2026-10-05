#include "../SCIO.H"
int main(void){char text[8192];int n=sc_auth_users(text,sizeof(text));return n<0?cli_error("users",n):cli_write(1,text,(u32)n)<0;}
