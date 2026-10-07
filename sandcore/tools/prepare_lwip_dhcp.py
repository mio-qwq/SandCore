#!/usr/bin/env python3
"""从固定BSD原件生成一处算术修正的私有DHCP翻译单元。"""
import argparse
import hashlib
from pathlib import Path

UPSTREAM_SHA256='f89cf1aacbd9a04dcb47a03c81a1a231331f4edb323d2e0cf26e640ca78c3453'
ORIGINAL=b'(dhcp->offered_t0_lease * 7U) / 8U'
CORRECTED=b'dhcp->offered_t0_lease - (dhcp->offered_t0_lease / 8U) - ((dhcp->offered_t0_lease % 8U) != 0U)'


def prepare(source,out):
    body=source.read_bytes()
    if hashlib.sha256(body).hexdigest()!=UPSTREAM_SHA256 or body.count(ORIGINAL)!=1:
        raise ValueError('固定lwIP DHCP原件改变，不能静默套用已审阅的算术修正')
    # t-ceil(t/8)与floor(7*t/8)相同，全部32位输入都不需要先乘7。
    # 保留原件每个版权/许可字节；完整实际派生源码同时归档客体。
    notice=('/* SandCore M10a1 私有派生：仅默认T2改为无溢出的整数等式。\n'
            ' * 上游固定原件SHA256: '+UPSTREAM_SHA256+'\n'
            ' * 完整上游版权/许可证如下；原件仍在third_party与SYS/LICENSE。 */\n').encode('utf-8')
    generated=notice+body.replace(ORIGINAL,CORRECTED)
    out.parent.mkdir(parents=True,exist_ok=True)
    if not out.exists() or out.read_bytes()!=generated:out.write_bytes(generated)
    return generated


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();body=prepare(args.source,args.out)
    print('BSD DHCP private derivative: '+str(len(body))+' bytes, sha256='+hashlib.sha256(body).hexdigest())


if __name__=='__main__':main()
