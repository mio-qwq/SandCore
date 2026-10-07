# ext —— SandCore 扩展生态工作区

> 本目录是主项目**之外**的扩展生态区：主项目文件零改动（只读），
> 一切扩展产物都放在这里。

## 分层

```
ext/
  README.md            本文件：ext 总览
  CONVENTIONS.md       ext 层约定（分层/命名/质量/状态声明纪律）
  libex/               跨包复用共享库（含 LIBEX.md 库合同文档）
  tools/               跨包构建工具（mkicon.py / mkpayload.py / mkscx.py）
  SoftwarePack1/       第一个扩展软件包（源码/文档/构建/成品 dist/）
```

- `libex/`、`tools/` 是**复用层**：SoftwarePack2 直接带走。
- `SoftwarePackN/` 是**包专属层**：应用源码、图标、配置模板、
  安装器、文档、dist 成品。包之间不共享应用源码。

## 现有包

SandCore_ExtraSoftware_Pack_1 已获用户完全验收；五个程序与安装器均补齐 About、copyright (c) mio 2026、https://github.com/mio-qwq/ 和 made with ❤️，作为无版本后缀的正式包结项。最终产物、哈希和本次离线回归记录见 [发布记录](SoftwarePack1/RELEASE.md)。baseline 保留在发布目录之外。

| 包 | 交付物 | 文档 |
|---|---|---|
| SandCore_ExtraSoftware_Pack_1 | dist/ 下单个安装器 SCX（PCalc / SandSheet / SandHex / SandBlocks / SandRaider） | [SoftwarePack1/README.md](SoftwarePack1/README.md) |

## 新增一个软件包的步骤（约定见 CONVENTIONS.md）

1. 建 `SoftwarePackN/`：apps 子目录 + icons + cfg + Makefile。
2. 应用界面基于 libex：`#include "NUI.inc"` + `exui_ext.inc`，
   计算用 exutil，音效用 exaudio。
3. 安装器复用 SoftwarePack1/installer.c 的向导骨架（拷贝改造，
   换 manifest）。
4. 文档三件套：包 README（是什么/怎么装/状态声明）、各应用
   README（操作/配置）、构建说明。
