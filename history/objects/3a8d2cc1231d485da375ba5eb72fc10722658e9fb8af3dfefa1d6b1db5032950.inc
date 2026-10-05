#include "IMAGECLIENT.H"
static void image_client_cancel(ScImageClient *client)
{
    if(client && client->ticket){sc_image_cancel(client->ticket);client->ticket=0;}
}
static int image_client_begin(ScImageClient *client,const char *path)
{
    if(!client || !path)return -1;
    image_client_cancel(client);
    int ticket=sc_image_request(path);
    client->pixels=0;client->error=ticket<0?ticket:0;
    client->ticket=ticket>0?(u32)ticket:0;
    return ticket>0?0:ticket;
}
static int image_client_step(ScImageClient *client)
{
    if(!client || !client->ticket)return client && client->pixels?0:-1;
    int result=sc_image_result(client->ticket,client->info,0,0);
    if(result==1)return 1;
    if(result<0){client->error=result;image_client_cancel(client);return result;}
    u32 w=client->info[1],h=client->info[2];
    if(client->info[0]!=1 || !w || !h || w>1920 || h>1080 || client->info[3]!=w*h*4
        || client->info[4]!=2 || client->info[5]!=client->ticket || client->info[6] || client->info[7]){
        client->error=-4;image_client_cancel(client);return -4;
    }
    u32 *candidate=sc_alloc(client->info[3]);
    if(!candidate){client->error=-5;image_client_cancel(client);return -5;}
    result=sc_image_result(client->ticket,client->info,candidate,client->info[3]);
    if(result){sc_free(candidate);client->error=result<0?result:-6;image_client_cancel(client);return client->error;}
    client->ticket=0;client->pixels=candidate;client->error=0;return 0;
}
