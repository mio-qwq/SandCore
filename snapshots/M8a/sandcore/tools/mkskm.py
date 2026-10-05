#!/usr/bin/env python3
"""mio：从零基址 ELF32 构建 SKM1MIO，不依赖第三方 ELF 库。

只接纳内部 PC 相对和绝对 32 位重定位；外部未定义符号和未知重定位
显式拒绝。纯二进制由 objcopy 生成，ELF 保留重定位给本工具提取。
符号表提供初始化入口和 BSS 尾部，装载器不需要理解 ELF。
"""
import struct
import sys
from pathlib import Path

def build(elf_path,bin_path,out_path):
    elf=Path(elf_path).read_bytes()
    header=struct.unpack_from('<16sHHIIIIIHHHHHH',elf)
    if header[0][:7]!=b'\x7fELF\x01\x01\x01' or header[2]!=3:
        raise ValueError('模块工具要求小端 ELF32/i386')
    sections=[struct.unpack_from('<10I',elf,header[6]+i*header[11]) for i in range(header[12])]
    symbols={}
    for sh in sections:
        if sh[1]!=2: continue
        strings=sections[sh[6]]
        table=elf[strings[4]:strings[4]+strings[5]]
        for offset in range(sh[4],sh[4]+sh[5],sh[9]):
            name,value,size,info,other,index=struct.unpack_from('<IIIBBH',elf,offset)
            label=table[name:table.find(b'\0',name)].decode()
            if label:
                if index==0: raise ValueError('不允许外部未定义符号 '+label)
                symbols[label]=value
    payload=Path(bin_path).read_bytes()
    if symbols['__module_image_end']!=len(payload): raise ValueError('平映像尾部与符号不一致')
    patches=[]
    for sh in sections:
        if sh[1]!=9 or not sections[sh[7]][2]&2: continue
        for offset in range(sh[4],sh[4]+sh[5],8):
            address,info=struct.unpack_from('<II',elf,offset)
            kind=info&255
            if kind==1: patches.append(address)
            elif kind!=2: raise ValueError('不支持的重定位 '+str(kind))
    patches=sorted(set(patches))
    for address in patches:
        if address+4>len(payload): raise ValueError('重定位在载荷外')
    bss=symbols['__module_end']-len(payload)
    output=b'SKM1MIO\0'+struct.pack('<6I',symbols['module_init'],len(payload),bss,len(patches),1,0x004F494D)
    output+=payload+b''.join(struct.pack('<I',address) for address in patches)
    path=Path(out_path); path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(output)
    print(f'mkskm: {len(payload)}B + bss {bss}B, {len(patches)} relocations -> {path}')

if __name__=='__main__': build(*sys.argv[1:])
