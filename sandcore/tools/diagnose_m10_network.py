#!/usr/bin/env python3
"""首轮无报文故障的受控诊断：公开ABI快照、真实抓包与HMP画面。

只改本次QEMU副本；上传当前诊断源码并记录摘要，不冒充原盘源码。
DHCP退出状态原样归档，诊断完成不代表网络验收通过。
"""
import argparse
import hashlib
import json
from pathlib import Path

from m10netpeer import FramePeer
from scserial import QemuSession
from verify_m9 import Guest
from verify_m10_network import failure_evidence, screenshot


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--boot', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]/'tests/m10/M10NET.C'
    content = source.read_bytes()
    report = dict(scope='FAILURE_DIAGNOSIS_ONLY_NOT_NETWORK_ACCEPTANCE',
                  fixture_sha256=hashlib.sha256(content).hexdigest(), commands=[])
    with FramePeer(args.out.with_name(args.out.name+'-peer')) as peer:
        with QemuSession(args.boot, args.data, args.out, 'tcg', True, audio=False,
                         network='socket', network_peer=peer.port, network_capture=True, memory=256) as vm:
            guest = Guest(vm)
            try:
                guest.connect()
                guest.put_bytes(content, '/TMP/M10DIAG.C', 'diagnostic-source')
                for text in ('s3c /TMP/M10DIAG.C /TMP/M10NET.SCX',
                             '/TMP/M10NET.SCX diagnostics', 'ifconfig en0',
                             'udhcpc -n -t 3', '/TMP/M10NET.SCX diagnostics', 'ifconfig en0'):
                    code, output, transcript, wall, cpu = guest.run(text, 240 if text.startswith('s3c ') else 20)
                    report['commands'].append(dict(script=text, exit_code=code, stdout_hex=output.hex(),
                                                   transcript=transcript.decode('utf-8', 'replace'),
                                                   wall_seconds=wall, qemu_cpu_seconds=cpu))
                    print(text + ': exit=' + str(code), flush=True)
                    print(output.decode('utf-8', 'replace'), flush=True)
                    if text.startswith('s3c ') and code:
                        raise RuntimeError('诊断源码客体编译失败')
                report['hmp_pci'] = vm.hmp('info pci')
                report['hmp_network'] = vm.hmp('info network')
                report['screenshots'] = []
                screenshot(vm, report, 'network-diagnostic')
                report['status'] = 'DIAGNOSTIC_COLLECTED_NOT_PASS'
            except BaseException as error:
                report.setdefault('screenshots', [])
                failure_evidence(vm, guest, report, error)
                raise
            finally:
                try:
                    guest.close()
                finally:
                    (vm.out/'diagnostic.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
                    peer.expect_disconnect()


if __name__ == '__main__':
    main()
