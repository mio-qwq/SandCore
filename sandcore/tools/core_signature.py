#!/usr/bin/env python3
"""仅验证用户公钥/签名并组装扩展；没有生成键或签署命令。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from mkext import check_message, SIGNATURE


def verify_signature_only(public_key, message, signature, openssl='openssl'):
    """独立密码学校验；错误格式夹具也须先证明签名确实来自用户。"""
    key = bytes(public_key)
    if len(key) != 32 or len(signature) != SIGNATURE:
        raise ValueError('公钥必须32字节、签名必须64字节；不接受私钥或PEM')
    y = int.from_bytes(key, 'little') & ((1 << 255) - 1)
    if y >= (1 << 255) - 19 or y in (0, 1, (1 << 255) - 20):
        raise ValueError('公钥非规范或明显低阶点')
    # RFC8410的Ed25519 SubjectPublicKeyInfo只包公开的32字节键。
    # OpenSSL作为独立宿主验签工具，不复制实现到内核/发行盘。
    with tempfile.TemporaryDirectory(prefix='sandcore-public-verify-') as directory:
        root = Path(directory)
        (root / 'public.der').write_bytes(bytes.fromhex('302a300506032b6570032100') + key)
        (root / 'message.bin').write_bytes(message)
        (root / 'signature.bin').write_bytes(signature)
        result = subprocess.run([openssl, 'pkeyutl', '-verify', '-rawin', '-pubin',
                                 '-keyform', 'DER', '-inkey', str(root / 'public.der'),
                                 '-in', str(root / 'message.bin'), '-sigfile', str(root / 'signature.bin')],
                                capture_output=True, timeout=30, check=False)
        if result.returncode:
            raise ValueError('用户签名未通过独立OpenSSL验签：' + result.stderr.decode(errors='replace')[:512])


def verify(public_key, message, signature, openssl='openssl'):
    # 正式发布入口始终先拒绝不支持的格式；签名有效不能扩大ABI或
    # 绕过重定位边界。纯验签入口仅供公开结果核对和受控拒绝测试。
    report = check_message(message)
    verify_signature_only(public_key, message, signature, openssl)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--message', type=Path, required=True)
    parser.add_argument('--signature', type=Path, required=True)
    parser.add_argument('--public-key', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--openssl', default='openssl')
    args = parser.parse_args()
    message, signature, key = args.message.read_bytes(), args.signature.read_bytes(), args.public_key.read_bytes()
    report = verify(key, message, signature, args.openssl)
    if args.out.exists():
        raise FileExistsError('输出已存在；不覆盖已签产物')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    output = message + signature
    with args.out.open('xb') as stream:
        stream.write(output)
    report.update(status='HOST_SIGNATURE_VERIFIED_RUNTIME_PENDING', key_sha256=hashlib.sha256(key).hexdigest(),
                  file_sha256=hashlib.sha256(output).hexdigest())
    args.out.with_suffix(args.out.suffix + '.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
