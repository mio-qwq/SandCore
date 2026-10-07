#include "management.h"
#include "serial.h"
#include "auth.h"
#include "streams.h"
#include "task.h"
#include "fs.h"
#include "crc.h"
#include "crypto.h"
#include "timer.h"
#define PAYLOAD_MAX 512u
#define FRAME_MAX (16u+PAYLOAD_MAX+4u)
#define CONSOLE_BYTES 16384u
enum { HELLO=1,INPUT,PUT_BEGIN,PUT_CHUNK,PUT_END,CANCEL,GET_BEGIN,GET_CHUNK,GET_END,PING,BYE };
static u8 receive[FRAME_MAX],encoded[FRAME_MAX*2+2],reply[PAYLOAD_MAX],cached[PAYLOAD_MAX];
static u32 received,escape,discard,tx_size,tx_position,last_rx,last_sequence,last_crc,cached_size;
static u16 cached_type;
static u32 session,transfer_token,transfer_size,transfer_offset,download_size,download_offset,download_generation;
static int shell_pid;
static char download_path[64];
static u8 expected_hash[32];
static sha256_t upload_hash,download_hash;
static u8 console[CONSOLE_BYTES];
static u32 console_head,console_tail;
static void stop_transfer(void)
{
    if(transfer_token)fs_stream_abort(0,transfer_token);
    transfer_token=transfer_offset=download_generation=download_offset=0;
    crypto_zero(&upload_hash,sizeof(upload_hash));crypto_zero(&download_hash,sizeof(download_hash));
    crypto_zero(expected_hash,sizeof(expected_hash));
}
static void disconnect(void)
{
    stop_transfer();session=0;auth_serial_end();streams_serial_reset();shell_pid=-1;
    console_head=console_tail=0;last_sequence=0;
}
void management_init(void)
{
    received=escape=discard=tx_size=tx_position=last_sequence=0;
    session=transfer_token=download_generation=console_head=console_tail=0;shell_pid=-1;
}
int management_connected(void){return session!=0;}
u32 management_console_capacity(void){return session?CONSOLE_BYTES-(console_head-console_tail):0;}
int management_console_write(const void *data,u32 length)
{
    if(!session)return -5;
    u32 count=CONSOLE_BYTES-(console_head-console_tail);if(count>length)count=length;
    for(u32 i=0;i<count;i++)console[(console_head+i)&(CONSOLE_BYTES-1)]=((const u8 *)data)[i];
    console_head+=count;return count?(int)count:-6;
}
int management_console_write_atomic(const void *data,u32 length)
{
    /* 清屏等控制序列必须一次排入正文队列，否则调用者重试会重复前缀，
     * 让终端把后续文本当成残缺CSI。当前在单核syscall内、IF关闭，
     * 检查和写入之间无另一生产者；普通文字仍保留部分写的字节流合同。 */
    if(!session)return -5;
    if(length>CONSOLE_BYTES-(console_head-console_tail))return -6;
    return management_console_write(data,length);
}
static void byte(u8 value)
{
    if(value==0x7E || value==0x7D){encoded[tx_size++]=0x7D;encoded[tx_size++]=value^0x20;}
    else encoded[tx_size++]=value;
}
static void send(u16 type,u32 sequence,const u8 *payload,u32 length)
{
    u8 header[16]={'S','C','9',0};*(u16 *)(header+4)=type;*(u16 *)(header+6)=0;
    *(u32 *)(header+8)=sequence;*(u32 *)(header+12)=length;
    u32 crc=crc32_update(0,header,16);crc=crc32_update(crc,payload,length);
    tx_position=tx_size=0;encoded[tx_size++]=0x7E;
    for(u32 i=0;i<16;i++)byte(header[i]);for(u32 i=0;i<length;i++)byte(payload[i]);
    for(u32 i=0;i<4;i++)byte((u8)(crc>>(i*8)));encoded[tx_size++]=0x7E;
}
static int path(const u8 *payload,u32 length,char out[64])
{
    if(!length || length>64 || payload[length-1])return -1;
    for(u32 i=0;i+1<length;i++)if(!payload[i])return -1;
    return fs_normalize((const char *)payload,out)>0?0:-1;
}
static void process(void)
{
    if(received<20 || receive[0]!='S' || receive[1]!='C' || receive[2]!='9' || receive[3])return;
    u16 type=*(u16 *)(receive+4);u32 sequence=*(u32 *)(receive+8),length=*(u32 *)(receive+12);
    if(*(u16 *)(receive+6) || !sequence || length>PAYLOAD_MAX || received!=20+length)return;
    u32 crc=crc32_update(0,receive,16+length);if(crc!=*(u32 *)(receive+16+length))return;
    if(sequence==last_sequence){
        /* stop-and-wait重发不能再次执行账户切换/文件提交/输入。不同
         * 正文却复用序号直接拒绝；对相同请求返回原来的确定结果。 */
        if(crc==last_crc)send(cached_type,sequence,cached,cached_size);return;
    }
    const u8 *data=receive+16;int result=-1;u32 extra=0;char name[64];
    if(type==HELLO && length==4 && *(u32 *)data==1){
        disconnect();session=auth_serial_begin();
        if(session){
            shell_pid=auth_serial_exec(session,"/BIN/SH.SCX --serial");
            if(shell_pid>0 && !streams_serial_attach(shell_pid,session))result=0;
            else {disconnect();result=-4;}
        }else result=-5;
        *(u32 *)(reply+4)=1;*(u32 *)(reply+8)=512;extra=8;
    }else if(!session)result=-5;
    else switch(type){
    case INPUT:result=streams_serial_input(data,length);break;
    case PUT_BEGIN:
        if(length>=37 && !path(data+36,length-36,name)){
            stop_transfer();transfer_size=*(u32 *)data;
            result=fs_stream_begin(0,name,transfer_size);
            if(result>0){transfer_token=(u32)result;transfer_offset=0;
                for(u32 i=0;i<32;i++)expected_hash[i]=data[4+i];sha256_init(&upload_hash);result=0;}
        }break;
    case PUT_CHUNK:
        if(length>=4 && transfer_token && *(u32 *)data==transfer_offset && length-4<=transfer_size-transfer_offset){
            result=fs_stream_write(0,transfer_token,data+4,length-4);
            if(result>=0){sha256_update(&upload_hash,data+4,length-4);transfer_offset+=length-4;}
            *(u32 *)(reply+4)=transfer_offset;extra=4;
        }break;
    case PUT_END:
        if(!length && transfer_token && transfer_offset==transfer_size){
            u8 digest[32];sha256_final(&upload_hash,digest);
            if(crypto_equal(digest,expected_hash,32)){result=fs_stream_commit(0,transfer_token);if(result>=0)result=0;}
            else result=-2;
            if(result<0)fs_stream_abort(0,transfer_token);transfer_token=0;crypto_zero(digest,32);
        }break;
    case CANCEL:if(!length){stop_transfer();result=0;}break;
    case GET_BEGIN:
        if(!path(data,length,name)){
            stop_transfer();u32 info[8];result=fs_metadata(name,info);
            if(!result && info[1]!=1)result=-1;
            if(!result){for(u32 i=0;i<64;i++)download_path[i]=name[i];download_size=info[2];download_generation=info[7];
                download_offset=0;sha256_init(&download_hash);*(u32 *)(reply+4)=download_size;*(u32 *)(reply+8)=download_generation;extra=8;}
        }break;
    case GET_CHUNK:
        if(length==4 && download_generation && *(u32 *)data==download_offset){
            u32 info[8];result=fs_metadata(download_path,info);
            if(!result && info[7]!=download_generation)result=-8;
            if(!result){u32 count=download_size-download_offset;if(count>500)count=500;
                result=fs_read_at(download_path,reply+8,count,download_offset);
                if(result>=0){*(u32 *)(reply+4)=download_offset;extra=4+(u32)result;
                    sha256_update(&download_hash,reply+8,(u32)result);download_offset+=(u32)result;}}
        }break;
    case GET_END:
        if(!length && download_generation && download_offset==download_size){
            sha256_final(&download_hash,reply+4);extra=32;download_generation=0;result=0;
        }break;
    case PING:if(!length)result=0;break;
    case BYE:if(!length){disconnect();result=0;}break;
    default:break;
    }
    *(i32 *)reply=result;cached_size=4+extra;cached_type=type|0x8000;
    for(u32 i=0;i<cached_size;i++)cached[i]=reply[i];last_sequence=sequence;last_crc=crc;last_rx=sc_ticks;
    send(cached_type,sequence,cached,cached_size);
}
void management_poll(void)
{
    if(session && (u32)(sc_ticks-last_rx)>3000)disconnect();
    /* TX帧可能比UART队列大，分多轮推进；从不忙等THRE或把中断关
     * 到整文件发完。管理请求停等一帧，控制回复优先于控制台输出。 */
    if(tx_position<tx_size){tx_position+=serial_write(SERIAL_MANAGEMENT,encoded+tx_position,tx_size-tx_position);return;}
    if(received && (u32)(sc_ticks-last_rx)>200){received=escape=discard=0;}
    u8 input;
    for(u32 budget=1024;budget && serial_read(SERIAL_MANAGEMENT,&input,1);budget--){
        if(input==0x7E){
            if(received && !discard){process();received=escape=discard=0;if(tx_position<tx_size)return;}
            received=escape=discard=0;continue;
        }
        last_rx=sc_ticks;if(discard)continue;
        if(input==0x7D){if(escape)discard=1;else escape=1;continue;}
        if(escape){input^=0x20;escape=0;}
        if(received==FRAME_MAX){discard=1;continue;}receive[received++]=input;
    }
    if(console_head!=console_tail && session){
        u32 count=console_head-console_tail;if(count>512)count=512;
        for(u32 i=0;i<count;i++)reply[i]=console[(console_tail+i)&(CONSOLE_BYTES-1)];
        console_tail+=count;streams_serial_writable();send(0x100,0,reply,count);
    }
}
