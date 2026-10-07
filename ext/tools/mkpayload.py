#!/usr/bin/env python3
"""mkpayload.py —— 把编译好的应用 SCX/图标/配置模板冻结成 C 载荷。

SandCore_ExtraSoftware_Pack_1 安装器是单个 SCX：全部应用以内嵌
载荷静态链接（SCX 载荷上限约 4MB）。manifest 每行：

    <名字> <app.scx> <icon.scb> <cfg模板> <描述>

生成 payload.h / payload.c：
    typedef struct {
        const char *name;        /* 安装名（SCX 主名） */
        const char *label;       /* 桌面快捷方式标签 */
        const char *cfg_name;    /* HOME 下配置文件名 */
        const u8   *scx;  const u32 scx_size, scx_crc;
        const u8   *icon; const u32 icon_size;
        const char *cfg;         /* 默认配置正文 */
        const char *desc;        /* 组件页描述 */
    } exapp_entry;
    extern const exapp_entry EXAPP[]; extern const int EXAPP_COUNT;

名字映射 label/cfg_name 在 manifest 里显式指定（第 6/7 列可选，
缺省取名字本身），避免安装器里硬编码 if-else 链。
"""
import sys
import re
import zlib


def c_array(data, per_line=20):
    rows = []
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        rows.append("    " + ",".join(f"0x{b:02X}" for b in chunk) + ",")
    return "\n".join(rows).rstrip(",")


def main():
    if len(sys.argv) != 4:
        print("usage: mkpayload.py <manifest> <payload.h> <payload.c>",
              file=sys.stderr)
        return 2
    apps = []
    # 描述字段带引号且可含空格：第 5 列起按引号截取，
    # 之后才是可选的标签与配置文件名列。
    pat = re.compile(
        r'^(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+"([^"]*)"\s*(\S+)?\s*(\S+)?\s*$')
    for line in open(sys.argv[1], encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = pat.match(line)
        if not m:
            print(f"mkpayload: bad line: {line}", file=sys.stderr)
            return 1
        name, scx, icon, cfg, desc = m.group(1, 2, 3, 4, 5)
        label = m.group(6) or name
        cfgname = m.group(7) or name + ".CFG"
        apps.append((name, scx, icon, cfg, desc, label, cfgname))
    if not apps:
        print("mkpayload: empty manifest", file=sys.stderr)
        return 1
    hdr = ["/* 自动生成：tools/mkpayload.py。载荷内嵌接口，勿手改。 */",
           "#ifndef PAYLOAD_H", "#define PAYLOAD_H",
           '#include "SCAPI.H"', "",
           "typedef struct {",
           "    const char *name;",
           "    const char *label;",
           "    const char *cfg_name;",
           "    const u8 *scx;",
           "    const u32 scx_size;",
           "    const u32 scx_crc;",
           "    const u8 *icon;",
           "    const u32 icon_size;",
           "    const char *cfg;",
           "    const char *desc;",
           "} exapp_entry;",
           "extern const exapp_entry EXAPP[];",
           "extern const int EXAPP_COUNT;",
           "#endif", ""]
    body = ['/* 自动生成：tools/mkpayload.py，勿手改。',
            ' * 每条记录注明长度与 CRC32；安装器写入前复核，双保险。 */',
            '#include "payload.h"', ""]
    for name, scx_path, icon_path, cfg_path, desc, label, cfgname in apps:
        sym = "".join(c if c.isalnum() else "_" for c in name).upper()
        scx = open(scx_path, "rb").read()
        icon = open(icon_path, "rb").read()
        cfg = open(cfg_path, "rb").read().decode("utf-8")
        if not scx.startswith(b"SCX1MIO\0"):
            print(f"mkpayload: {scx_path} is not SCX", file=sys.stderr)
            return 1
        if not icon.startswith(b"SCB2MIO\0"):
            print(f"mkpayload: {icon_path} is not SCB2", file=sys.stderr)
            return 1
        crc = zlib.crc32(scx) & 0xFFFFFFFF
        body += [f"/* {name}: {len(scx)}B crc32={crc:08X} icon={len(icon)}B */",
                 f"static const u8 PAYLOAD_{sym}_SCX[{len(scx)}]={{",
                 c_array(scx), "};",
                 f"static const u8 PAYLOAD_{sym}_ICON[{len(icon)}]={{",
                 c_array(icon), "};",
                 f"static const char PAYLOAD_{sym}_CFG[]="]
        for line in cfg.splitlines():
            esc = line.replace("\\", "\\\\").replace('"', '\\"')
            body.append(f'    "{esc}\\n"')
        body.append("    ;")
        desc_esc = desc.replace('"', "'")
        body += [f'static const char PAYLOAD_{sym}_DESC[]="{desc_esc}";', ""]
    body.append("const exapp_entry EXAPP[]={")
    for name, scx_path, icon_path, cfg_path, desc, label, cfgname in apps:
        sym = "".join(c if c.isalnum() else "_" for c in name).upper()
        crc = zlib.crc32(open(scx_path, "rb").read()) & 0xFFFFFFFF
        body += [f'    {{"{name}","{label}","{cfgname}",',
                 f"     PAYLOAD_{sym}_SCX,sizeof(PAYLOAD_{sym}_SCX),"
                 f"0x{crc:08X}u,",
                 f"     PAYLOAD_{sym}_ICON,sizeof(PAYLOAD_{sym}_ICON),",
                 f'     PAYLOAD_{sym}_CFG,PAYLOAD_{sym}_DESC}},']
    body += ["};",
             f"const int EXAPP_COUNT={len(apps)};", ""]
    open(sys.argv[2], "w", encoding="utf-8", newline="\n").write("\n".join(hdr))
    open(sys.argv[3], "w", encoding="utf-8", newline="\n").write("\n".join(body))
    print(f"mkpayload: {len(apps)} apps -> {sys.argv[2]} + {sys.argv[3]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
