# SandCore_ExtraSoftware_Pack_1 构建指南

## 工具链

| 平台 | 命令 | 产物格式 | 状态 |
|---|---|---|---|
| WSL Ubuntu（主力） | `make CC=gcc` | ELF → 平二进制 | 本包实际使用 |
| Windows MinGW（备用） | `mingw32-make CC=gcc` | PE → 平二进制（-j 挑段） | 语法/链接验证通过 |

与主项目 sandcore/Makefile 的用户程序链完全一致：
`gcc`（CFLAGS 逐字相同）→ `ld -T libex/linker.ld` → `objcopy` →
`tools/mkscx.py`（含 `--icon`）。

## 产物流

```
icons/<app>.txt ──mkicon.py──> build/<app>.scb            （SCB2 图标）
<app>/<app>.c ──gcc──> build/<app>.o ─┐
libex/exutil.c/exaudio.c ──gcc──> .o ─┤ start.o
libex/start.asm ──nasm──> start.o ────┘
   ──ld -T linker.ld──> .pe/.elf ──objcopy──> .bin
   ──mkscx.py --icon──> build/<app>.scx
manifest.txt + 5×scx + 5×scb ──mkpayload.py──> installer/payload.{h,c}
installer.c + payload.o ──ld/objcopy/mkscx──> dist/SandCore_ExtraSoftware_Pack_1.scx
```

## 平台坑（宝贵经验，勿删）

1. **libgcc 辅助符号**：i386 上 `long long` 除法/取模、
   `double↔i64` 转换会引用 `__udivdi3/__divdi3/__fixdfdi`，
   `-nostdlib` 下链接失败。禁用之；大数用 u32 分段（sheet 的
   `sh_fmt`）。
2. **MinGW `__main` 桩**：GCC 在 `main` 序言发出 `call __main`
   （CRT 构造桩）。freestanding 无 CRT → start.asm 的 WIN_COFF
   分支提供 `global __main; ret` 空桩。
3. **MinGW 模式规则缺陷**：目标与前置都带目录的隐式模式规则
   （`build/%.o: %/%.c`）在 mingw32-make 4.4.1 下匹配异常
   （前置替换后残留 `%`）。用 `foreach/eval` 显式生成规则。
4. **WSL 无 `python`** 只有 `python3`：Makefile 用
   `PY ?= python3`（Windows 侧 `make PY=python` 覆盖）。
5. **build 目录必须存在**再调 nasm（WSL 侧 nasm 不自动建目录）。

## 复现验证清单（重构建后）

- `mkscx.py` 对每个 SCX 的载荷/符号一致性校验（构建即校验）。
- SCX 头：`magic/entry=0/load/bss/stack=131072/flags bit1/base`
  逐字段核对（见 README §6 的实测值）。
- 图标：24×24、2336B、`SCB2MIO\0` 头 + `MIO\0` 尾签。
- 安装器载荷 CRC32（payload.c 内注释值 vs 运行时 ins_crc32）。

## 清理

`make clean` 删除 build/、dist/ 与生成的 payload.{h,c}。
