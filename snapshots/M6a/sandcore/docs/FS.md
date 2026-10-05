# SandCore 磁盘格式规范 v0.2（M6a）：SandFS + SCF 字体 + SCX 可执行

> **作者: mio** ｜ 数据盘 `sanddata.img` 挂 QEMU IDE (主盘), 引导盘
> `sandcore.img` 挂软盘 —— 两者分离 (M5 实录: ATA 驱动够不着 FDC 软盘)。
> 所有魔数带作者尾缀 **MIO**。

## 1. SandFS v1（只读文件系统）

| 区域 | LBA | 内容 |
|---|---|---|
| 超级块 | 0 | 魔数 `SANDFSMIO`(9B) + 保留(3B) + 文件数(u32 @12) |
| 目录表 | 1 | 每项 40B: 文件名(31B UTF-8 + NUL) + start_lba(u32) + size(u32) |
| 数据 | 2+ | 文件内容顺序排列, 每个文件对齐到 512B |

- 上限 12 个文件 (目录表一扇区), 单文件不限 (受盘容量);
- **伪目录** (v0.2): 文件名允许带路径前缀, 约定命名区:
  `sys/` 系统文件(font.scf) ｜ `bin/` 可执行命令 ｜ `apps/` 应用 ｜ `desk/` 桌面快捷方式(.lnk)
  —— 没有真正的目录结构, 只是名字前缀; 真目录树 = M6b+;
- **可写** (v0.2): `fs_write()` 已存在已重写则原地覆盖, 放不下/新文件追加到数据末端,
  超级块计数与目录表自动写回 (`write_meta()`); ATA 写 = `ata_write_sectors()`
  (PIO 逐扇 + 0xE7 FLUSH CACHE);
- 内核侧: `fs_init()` 用 ATA PIO 驱动读超级块与目录表;
- 上盘工具: `tools/mkfs.py` v2 —— 递归 `build/fs/` 目录树, 相对路径即文件名。
  【待办·交接】mkfs 仍内置追加 `"font.scf"`, 而 main.c 读 `"sys/font.scf"` —— 名字不匹配,
  修法见根目录 HANDOFF.md §3.2。

## 2. SCF1MIO 字体文件

```
偏移  大小  内容
0     7    魔数 "SCF1MIO"
8     4    字形数 (u32 LE)
12    ...  每字一条: UTF-8 汉字(3B)+NUL(1B) + 16 行 × (16 列 + NUL)
```

- 来源: `kernel/font16.txt` (人画的文本艺术) 由 mkfs.py 导出;
- 内核开机 `fs_read("font.scf")` → `gfx_font_load()` 激活为默认字体;
- 读不到文件则回落编译在内核里的内置表 —— **渲染器零改动**,
  这就是字体接口: 换字体 = 换盘上的文件。

## 3. SCX1MIO 可执行格式

```
偏移  大小  内容
0     8    魔数 "SCX1MIO\0"
8     4    entry   入口 RVA (相对装载基址)
12    4    load_size  装载字节数
16    4    bss_size    装载后清零的字节数
20    4    stack_size  建议栈大小
24    4    flags       bit0: 位置无关 (v1 未用)
28    4    base        装载基址 (v1 固定 0x400000)
32    4    署名 "MIO\0"
36    ...  装载映像 (平二进制)
```

- 加载器: `task.c` 的 `task_run_scx(name)` → 读文件 → 拷映像到
  用户槽 (0x400000, 已从页分配器预留) → 清 bss → `task_spawn` 建
  ring3 任务 (入口 = base + entry);
- 用户程序写法见 docs/SYSCALL.md §4。

## 4. 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-10-02 | 初版：SandFS 版图、SCF1MIO 字体、SCX1MIO 可执行、MIO 魔数与署名 (mio) |
| v0.2 | 2026-10-02 | M6a：SandFS 可写（fs_write/目录表写回/ATA 写）、伪目录命名区（sys/bin/apps/desk）、mkfs 走 build/fs 目录树；标注 font.scf 路径待办 |
