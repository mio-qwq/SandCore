#!/usr/bin/env python3
"""SandCore 磁盘镜像打包器
把引导扇区（扇区 0）和内核（扇区 1 起）写进一张 1.44MB 软盘镜像。
布局由 boot/boot.asm 里的 KERNEL_LBA / KERNEL_SECTS 约定。"""
import sys

BOOT_MAX = 512
KERNEL_SECTS = 640         # M9 容量合同：与boot.asm/linker.ld/docs/BOOT.md同步
FLOPPY_SIZE = 1474560      # 1.44MB

def main():
    if len(sys.argv) != 4:
        print("usage: mkimg.py <boot.bin> <kernel.bin> <out.img>")
        return 1
    boot, kern, out = sys.argv[1:4]

    b = open(boot, "rb").read()
    if len(b) > BOOT_MAX:
        sys.exit(f"boot sector too big: {len(b)} > {BOOT_MAX}")

    k = open(kern, "rb").read()
    if len(k) % 512:
        k += b"\x00" * (512 - len(k) % 512)
    if len(k) > KERNEL_SECTS * 512:
        sys.exit(f"kernel too big: {len(k)} > {KERNEL_SECTS * 512}")

    img = bytearray(FLOPPY_SIZE)
    img[:512] = b
    img[512:512 + len(k)] = k
    open(out, "wb").write(img)
    print(f"boot={len(b)}B  kernel={len(k)}B ({len(k)//512} sectors)  -> {out}")
    return 0

sys.exit(main())
