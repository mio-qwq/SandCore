#ifndef SANDCORE_IMAGE_CLIENT_H
#define SANDCORE_IMAGE_CLIENT_H
#include "SCAPI.H"
/* mio：可复用源级客户端，不依赖NUI/窗口/全局图像。调用者持有
 * 上下文，先零初始化；step非阻塞，不等待tick，不吞键盘/鼠标。
 * 成功后pixels属于调用者，必须sc_free；cancel只取消作业，不释放
 * 已成功交给调用者的图。API的32B快照使用独立数组，不把它嵌入旧
 * SCB头。begin返回0或负错误；step返回0完成、1等待、负错误。
 */
typedef struct {u32 ticket,info[8];u32 *pixels;int error;} ScImageClient;
static int image_client_begin(ScImageClient *client,const char *path);
/* 旧FSREAD/FSSTAT与图形工具身份使用根相对路径；该追加助手显式
 * 加根斜杠再请求，不改变begin/IMAGEREQUEST结合cwd的公开语义。
 * 路径正文最多63B；完整拒绝超长串，不截断成另一文件。 */
static int image_client_begin_root(ScImageClient *client,const char *path);
static int image_client_step(ScImageClient *client);
static void image_client_cancel(ScImageClient *client);
#endif
