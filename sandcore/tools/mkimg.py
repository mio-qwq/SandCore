#!/usr/bin/env python3
"""SandCore 磁盘镜像打包器
把引导扇区（扇区 0）和内核（扇区 1 起）写进一张 1.44MB 软盘镜像。
布局由 boot/boot.asm 里的 KERNEL_LBA / KERNEL_SECTS 约定。"""
import argparse

BOOT_MAX = 512
KERNEL_SECTS = 640         # M9 容量合同：与boot.asm/linker.ld/docs/BOOT.md同步
FLOPPY_SIZE = 1474560      # 1.44MB

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('boot')
    parser.add_argument('kernel')
    parser.add_argument('out')
    parser.add_argument('--sectors', type=int, choices=(128, 640), default=KERNEL_SECTS)
    args = parser.parse_args()
    boot, kern, out = args.boot, args.kernel, args.out

    b = open(boot, "rb").read()
    if len(b) != BOOT_MAX or b[-2:] != b'\x55\xaa':
        raise ValueError('引导扇区必须512字节且有55AA魔数')

    k = open(kern, "rb").read()
    if len(k) % 512:
        k += b"\x00" * (512 - len(k) % 512)
    if not k or len(k) > args.sectors * 512:
        raise ValueError(f"image too big or empty: {len(k)} / {args.sectors * 512}")

    img = bytearray(FLOPPY_SIZE)
    img[:512] = b
    img[512:512 + len(k)] = k
    open(out, "wb").write(img)
    print(f"boot={len(b)}B  kernel={len(k)}B ({len(k)//512} sectors)  -> {out}")
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
