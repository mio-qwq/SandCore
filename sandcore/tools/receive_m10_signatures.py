#!/usr/bin/env python3
"""核对用户公开结果；正常扩展和故意错误夹具分别保存，不接收私钥。"""
import argparse
import hashlib
import json
from pathlib import Path
import traceback

from core_signature import verify, verify_signature_only
from mkext import check_message

NAMES = ('Z10-INIT.SKM.msg', 'A20-SERVICE.SKM.msg', 'B30-FAIL.SKM.msg',
         'WALL100.SKM.msg', 'BAD-ABI.SKM.msg', 'BAD-RELOC.SKM.msg')
INVALID = {'BAD-ABI.SKM.msg', 'BAD-RELOC.SKM.msg'}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def indexed(items, expected):
    if len(items) != len(expected) or {item['file'] for item in items} != set(expected):
        raise ValueError('公开结果文件集合或数量不符')
    return {item['file']: item for item in items}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--results', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--openssl', default='openssl')
    args = parser.parse_args()
    manifest = json.loads((args.bundle/'manifest.json').read_text(encoding='utf-8'))
    public = json.loads((args.results/'PUBLIC-RESULTS.json').read_text(encoding='utf-8'))
    if manifest['algorithm'] != 'Ed25519' or public['algorithm'] != 'Ed25519':
        raise ValueError('只接收约定的Ed25519公开结果')
    declared = indexed(manifest['files'], NAMES)
    messages = indexed(public['messages'], NAMES)
    signatures = indexed(public['signatures'], tuple(name+'.sig' for name in NAMES))
    # 只打开约定的公开文件，既不遍历用户目录，也没有私钥参数或入口。
    key = (args.results/'public-key.bin').read_bytes()
    if len(key) != 32 or digest(key) != public['public_key_sha256']:
        raise ValueError('公开密钥长度或摘要不符')
    args.out.mkdir(parents=True, exist_ok=False)
    report = dict(status='VERIFYING', private_key_access='NONE',
                  verifier='OpenSSL pkeyutl -verify -rawin',
                  public_key_sha256=digest(key), bundle=str(args.bundle.resolve()),
                  public_results=str(args.results.resolve()), records=[])
    try:
        for name in NAMES:
            message = (args.bundle/name).read_bytes()
            signature = (args.results/(name+'.sig')).read_bytes()
            if digest(message) != declared[name]['message_sha256'] or digest(message) != messages[name]['sha256']:
                raise ValueError('待签正文与用户公开记录不匹配：'+name)
            if len(signature) != 64 or digest(signature) != signatures[name+'.sig']['sha256']:
                raise ValueError('签名长度或摘要不匹配：'+name)
            verify_signature_only(key, message, signature, args.openssl)
            # 摘要相同不等于验签：实际改动正文一个字节，独立验证必须
            # 拒绝；随后只保存原始签署正文，篡改版本不进入正式目录。
            tampered = bytearray(message)
            tampered[-1] ^= 1
            try:
                verify_signature_only(key, tampered, signature, args.openssl)
            except ValueError:
                pass
            else:
                raise AssertionError('独立工具错误接受了篡改正文')
            invalid = name in INVALID
            if bool(declared[name].get('invalid_format_intended')) != invalid:
                raise ValueError('错误夹具标记不符')
            if invalid:
                try:
                    check_message(message)
                except ValueError as error:
                    rejection = str(error)
                else:
                    raise AssertionError('故意错误格式没有被正式装载边界拒绝')
                metadata = dict(format_rejected=True, reason=rejection)
            else:
                metadata = verify(key, message, signature, args.openssl)
            destination = args.out/('rejection-fixtures' if invalid else 'signed')/name.removesuffix('.msg')
            destination.parent.mkdir(exist_ok=True)
            with destination.open('xb') as output:
                output.write(message+signature)
            record = dict(message=name, message_sha256=digest(message),
                          signature_sha256=digest(signature), signature_verified=True,
                          tampered_message_rejected=True, output=str(destination.resolve()),
                          output_sha256=digest(message+signature))
            record.update(metadata)
            report['records'].append(record)
        with (args.out/'public-key.bin').open('xb') as output:
            output.write(key)
        report['status'] = 'SIX_USER_SIGNATURES_HOST_VERIFIED_RUNTIME_PENDING'
    except BaseException as error:
        report.update(status='FAIL_OR_INTERRUPTED', error=dict(type=type(error).__name__,
                      message=str(error), traceback=traceback.format_exc()))
        raise
    finally:
        (args.out/'receipt.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(status=report['status'], records=len(report['records']),
                         public_key_sha256=report['public_key_sha256']), ensure_ascii=False), flush=True)


if __name__ == '__main__':
    main()
