/* =====================================================================
 *  SandCore 内核链接脚本 —— 两套工具链共用一份
 *  ---------------------------------------------------------------------
 *  内核最终产物是"纯二进制镜像"(flat binary):
 *    - 没有任何文件头, 从第一个字节开始就是可执行代码
 *    - 引导器把它原样搬到物理地址 0x10000, 然后跳过去
 *  所以此脚本的任务只有一个: 告诉链接器 "把所有东西按这个地址
 *  布局摆好", 之后 objcopy -O binary 沿着地址线把内容抻直输出。
 *
 *  两套工具链的差异由 Makefile 的命令行参数吸收:
 *    WSL/Linux : ld -m elf_i386       (正统 ELF, 输出 kernel.elf)
 *    Windows   : ld -m i386pe ...     (PE, 输出 kernel.pe, 备用路线)
 *  因此这里不写 OUTPUT_FORMAT —— 格式跟着命令行走。
 *
 *  【内存布局图】(地址从低到高)
 *    0x10000  +------------------+
 *             | .text   代码     |
 *             | .rodata 常量/字体|
 *             | .data  已初始化数据|
 *             | (对齐空隙, 全 0) |
 *             | .bss  <- 不占文件, 引导后由 entry.asm 清零
 *    0x10000+ +------------------+
 * =====================================================================
 */
ENTRY(_start)
SECTIONS
{
  . = 0x10000;                /* 内核装载基址, 与 boot.asm 的 KERNEL_SEG 约定一致 */

  .text   : { *(.text*) }     /* 代码 (entry.asm 的 _start 必须最先, 排在链接行首位) */
  .rodata : { *(.rodata*) }   /* 只读数据: 字体表、字符串 */
  .data   : { *(.data*) }     /* 已初始化数据 */

  /* M7 画布与 SCX 中转不能再挤占 0x90000 下方的引导栈。
   * BSS 无盘上内容，搬到 1MB 不增加镜像空洞；entry.asm 在 A20 已开启
   * 后清零这里。2MB 以下原本就是分配器保留区，无额外抢占用户帧。 */
  ASSERT(. <= 0x60000, "kernel initialized image exceeds boot contract")
  . = 0x100000;
  __bss_start = .;
  .bss (NOLOAD) : { *(.bss*) *(COMMON) }
  __bss_end = .;
  ASSERT(__bss_end <= 0x200000, "kernel BSS exceeds reserved low 2MB")

  /* 工具链可能顺手塞进来的"赠品", 内核用不到, 全部丢弃:
   *   .note.gnu.build-id : ELF 链接器默认附加的构建指纹
   *   .comment           : 编译器版本字符串
   *   .eh_frame          : C++ 异常展开表 (纯 C 内核用不到)      */
  /DISCARD/ : { *(.note*) *(.comment*) *(.eh_frame*) }
}
