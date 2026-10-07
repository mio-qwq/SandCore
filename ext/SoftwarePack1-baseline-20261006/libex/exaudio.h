/* =====================================================================
 * exaudio.h —— 音频包装与程序化音效（blocks / raider 共用）。
 *
 * 内核合同（SYSCALL.md 0x230..0x235）：48000Hz S16 PCM，普通 UID
 * 最多两路 voice；submit 非阻塞返回实际接收帧数。这里封装成
 * "生成一段音效 -> 尽量喂入 -> 剩余尾巴 pump 续喂"的简单模型，
 * 无声卡时全部安全退化为空操作。
 * ===================================================================== */
#ifndef EXAUDIO_H
#define EXAUDIO_H
#include "SCAPI.H"

typedef short s16;   /* SCAPI.H 只有无符号别名；PCM 样本是有符号 16 位 */

/* 波形种类：0 方波 1 噪声 2 三角波 */
#define EXWAVE_SQUARE 0
#define EXWAVE_NOISE  1
#define EXWAVE_TRIANGLE 2

/* 探测设备并打开一条 48000Hz 双声道 voice。返回 0 有声卡可用，
 * -1 无设备（后续调用全部安全空转）。 */
int  exaudio_init(void);
void exaudio_shutdown(void);

/* 提交一段 S16 双声道交错样本。返回实际入队帧数（可能小于
 * frames）；samples 须在完全播完前保持有效（游戏音效都是
 * 静态/常驻缓冲，满足此约定）。无声卡返回 0。 */
u32  exaudio_stream(const s16 *samples,u32 frames);

/* 把上次没喂完的尾巴续喂进去；游戏循环每帧调用一次。 */
void exaudio_pump(void);

/* 系统音效（START/STOP/NOTICE/ERROR/COMPLETE/QUESTION = 0..5）。 */
int  exaudio_sysfx(int kind);

/* 程序化生成并播放一段音效。volume 0..256；返回 0 已生成
 * （不管声卡是否存在），-1 参数错误/堆不足。 */
int  exaudio_tone(int freq_hz,int ms,int wave,int volume);

#endif /* EXAUDIO_H */
