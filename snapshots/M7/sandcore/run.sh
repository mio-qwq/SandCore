#!/bin/sh
# SandCore OS —— 从 Git Bash 运行
cd "$(dirname "$0")"
[ -f build/sandcore.img ] || { echo "先构建: mingw32-make"; exit 1; }
[ -f build/sanddata.img ] || { echo "先构建数据盘"; exit 1; }
"/c/Program Files/qemu/qemu-system-i386.exe" -drive format=raw,if=floppy,file=build/sandcore.img -drive format=raw,if=ide,file=build/sanddata.img
