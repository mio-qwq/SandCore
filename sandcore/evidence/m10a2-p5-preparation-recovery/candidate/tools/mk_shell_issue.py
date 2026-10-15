#!/usr/bin/env python3
"""从实际发行动态host生成CORE内的公开内容摘要；不生成/读取任何私钥。"""
import argparse
import hashlib
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import scd_format

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    body=args.host.read_bytes()
    contract=scd_format.validate_scx(body)
    if args.host.name.upper()!='SCSHELL.SCX' or len(body)>262144:
        raise ValueError('只接实际无图标的固定SCSHELL动态SCX发行映像')
    if not any(d['name']=='SCSHELL.SCL' for d in contract['dependencies']):
        raise ValueError('正式host必须实际依赖SCSHELL.SCL，不是临时空角色夹具')
    value=hashlib.sha256(body).digest()
    if not any(value):raise ValueError('不得产生全零通配摘要')
    text=('#ifndef SANDCORE_SHELL_HOST_ISSUE_H\n#define SANDCORE_SHELL_HOST_ISSUE_H\n'
        'static const u32 shell_host_issued_bytes=%du;\n' % len(body)
        +'static const u8 shell_host_issued_sha256[32]={'+','.join('0x%02x'%x for x in value)+'};\n#endif\n')
    args.out.parent.mkdir(parents=True,exist_ok=True)
    if not args.out.is_file() or args.out.read_text(encoding='ascii')!=text:
        args.out.write_text(text,encoding='ascii',newline='\n')
    print('ISSUED_RING3_HOST_BYTES',len(body),'SHA256',value.hex())
if __name__=='__main__':main()
