#!/usr/bin/env python3
"""只把用户提供的32字节Ed25519原始公钥固定进构建头；不处理任何私钥。"""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--public-key', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    data = args.public_key.read_bytes() if args.public_key else bytes(32)
    if len(data) != 32:
        raise ValueError('必须是32字节原始公钥；PEM、私钥与其它长度均拒绝')
    if args.public_key and (int.from_bytes(data, 'little') & ((1 << 255) - 1)) >= (1 << 255) - 19:
        raise ValueError('公钥点不是规范编码')
    configured = int(args.public_key is not None)
    content = ('/* 用户公钥构建副本；未配置时CORE扩展全部拒绝。 */\n'
               '#ifndef SANDCORE_CORE_PUBLIC_KEY_H\n#define SANDCORE_CORE_PUBLIC_KEY_H\n'
               f'#define CORE_PUBLIC_KEY_CONFIGURED {configured}\n'
               'static const unsigned char core_public_key[32]={' +
               ','.join(str(value) for value in data) + '};\n#endif\n')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    if not args.out.exists() or args.out.read_text(encoding='utf-8') != content:
        args.out.write_text(content, encoding='utf-8')


if __name__ == '__main__':
    main()
