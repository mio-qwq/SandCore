#!/usr/bin/env python3
"""先检查宿主构建工具，缺失时给出具体安装入口，不开始残缺构建。"""
import argparse
from pathlib import Path
import shutil
import subprocess

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--nasm',required=True);p.add_argument('--art-python',required=True);a=p.parse_args()
    missing=[name for name in ('gcc','ld','objcopy','nm','make') if not shutil.which(name)]
    if not shutil.which(a.nasm) and not Path(a.nasm).is_file():missing.append('nasm')
    if missing:raise SystemExit('缺构建工具 '+', '.join(missing)+'；WSL Ubuntu: sudo apt install build-essential binutils nasm python3 python3-pil')
    if not a.art_python:raise SystemExit('资源生成需要Pillow；WSL安装python3-pil，或将带Pillow的Windows python.exe放入PATH')
    result=subprocess.run([a.art_python,'-c','import PIL;print(PIL.__version__)'],capture_output=True,text=True)
    if result.returncode:raise SystemExit('资源解释器缺Pillow: '+a.art_python+'；安装python3-pil或python -m pip install Pillow')
    print('Build tools ready; resource Python '+a.art_python+'; Pillow '+result.stdout.strip())

if __name__=='__main__':main()
