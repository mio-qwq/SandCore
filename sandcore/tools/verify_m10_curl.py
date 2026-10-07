#!/usr/bin/env python3
"""只运行追加curl的有限HTTP用例，并在同一VM保留最终桌面图。"""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import threading

from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm
from verify_m10_network import Cases, connect_ready, failure_evidence
from m10_guest_boot import unique_symbols

ROOT = Path(__file__).resolve().parents[1]
PAYLOAD = bytes(range(256))*16+b'M10-curl\r\n\x00done\n'


class Peer(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def reply(self, code, body, head=False, location=None):
        self.send_response(code)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Connection', 'close')
        self.send_header('X-M10-Curl', 'okay')
        if location:
            self.send_header('Location', location)
        self.end_headers()
        if not head:
            self.wfile.write(body)
        self.close_connection = True

    def record(self, body=b''):
        self.server.observed.append(dict(method=self.command, path=self.path,
                                        custom=self.headers.get('X-M10-Test'),
                                        content_type=self.headers.get('Content-Type'),
                                        bytes=len(body), sha256=hashlib.sha256(body).hexdigest()))

    def do_GET(self):
        self.record()
        if self.path == '/redirect':
            self.reply(302, b'', location='/body')
        elif self.path == '/missing':
            self.reply(404, b'missing\n')
        else:
            self.reply(200, PAYLOAD)

    def do_HEAD(self):
        self.record()
        self.reply(200, PAYLOAD, head=True)

    def echo(self):
        size = int(self.headers.get('Content-Length', '0'))
        if not 0 <= size <= 1048576:
            raise ValueError('Unexpected upload size')
        body = self.rfile.read(size)
        if len(body) != size:
            raise ValueError('Truncated upload')
        self.record(body)
        self.reply(200, body)

    do_POST = echo
    do_PUT = echo
    do_PATCH = echo


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    boot = ROOT/'build/m10a1/sandcore.img'
    data = ROOT/'build/m10a1/sanddata.img'
    build = ROOT/'build/m10a1-fault-desktop-fix-01'
    symbols = unique_symbols(build/'core.sym')
    report = dict(status='RUNNING', scope='CURL_ADDITION_AND_ONE_FINAL_STARTUP',
                  cases=[], screenshots=[], source_sha256=sha(data),
                  core_sha256=sha(build/'fs/SYS/CORE/CORE.SKM'),
                  unrelated_components='REUSED_EXISTING_EVIDENCE_NOT_RERUN')
    server = ThreadingHTTPServer(('127.0.0.1', 0), Peer)
    server.observed = []
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with QemuSession(boot, data, args.out, 'tcg', True, audio=False,
                         network='user', memory=256) as vm:
            guest = Guest(vm)
            cases = Cases(guest, report, symbols)
            try:
                connect_ready(guest, symbols, report)
                cases.run('address-ready', 'udhcpc -n -t 20', contains=b'10.0.2.', timeout=40)
                code, _, _, _ = guest.command('mkdir -p /TMP/CURL/net')
                if code:
                    raise RuntimeError('Cannot prepare private curl source directory')
                # 上传本次源码到私有目录，三份头保持与正式SRC相同的相对布局。
                for name in ('SCAPI.H', 'SCIO.H', 'SCNET.H'):
                    guest.put_bytes((ROOT/'user'/name).read_bytes(), '/TMP/CURL/'+name, name)
                for name in ('curl.c', 'NETCLI.inc', 'HTTP.inc'):
                    guest.put_bytes((ROOT/'user/net'/name).read_bytes(), '/TMP/CURL/net/'+name, name)
                cases.run('native-curl-compile', 's3c /TMP/CURL/net/curl.c /TMP/CURL.SCX', timeout=240)
                cases.run('native-curl-help', '/TMP/CURL.SCX --help', contains=b'curl: ')
                guest.put_bytes(PAYLOAD, '/TMP/CURLDATA', 'upload')
                base = 'http://10.0.2.2:'+str(server.server_address[1])
                run = '/TMP/CURL.SCX -m 10 '
                cases.run('GET-native', run+base+'/body', expected=PAYLOAD)
                cases.run('GET-installed-build', 'curl -m 10 '+base+'/body', expected=PAYLOAD)
                cases.run('HEAD-and-dump', run+'-sSI -D /TMP/CURLHEAD '+base+'/body', contains=b'HTTP/1.1 200')
                head = guest.get_bytes('/TMP/CURLHEAD', 'head')
                cases.check('HEAD-header-bytes', b'Content-Length: '+str(len(PAYLOAD)).encode() in head and head.endswith(b'\r\n\r\n'), bytes=len(head))
                cases.run('POST-binary-custom-header', run+'-sS -X POST -H "X-M10-Test: native" -H "Content-Type: application/octet-stream" --data-binary @/TMP/CURLDATA -o /TMP/CURLOUT '+base+'/echo', expected=b'')
                output = guest.get_bytes('/TMP/CURLOUT', 'post')
                cases.check('POST-exact-body-and-header', output == PAYLOAD and any(x['method']=='POST' and x['custom']=='native' and x['bytes']==len(PAYLOAD) for x in server.observed), sha256=hashlib.sha256(output).hexdigest())
                cases.run('PUT-file', run+'-T /TMP/CURLDATA '+base+'/echo', expected=PAYLOAD)
                cases.run('POST-form', run+'-d "hello=world" '+base+'/echo', expected=b'hello=world')
                cases.run('GET-include-redirect', run+'-sSLi '+base+'/redirect', contains=PAYLOAD)
                cases.run('HTTP-fail-exit', run+'-fsS '+base+'/missing', code=22, expected=b'')
                cases.run('HTTPS-explicitly-unsupported', run+'-sS https://10.0.2.2/body', code=1, expected=b'')
                picture = vm.out/'desktop.ppm'
                vm.hmp('screendump "'+picture.as_posix()+'"')
                report['screenshots'].append(png_from_ppm(picture))
                report['http_requests'] = server.observed
                report['status'] = 'DECLARED_CURL_AND_STARTUP_PASS'
            except BaseException as error:
                failure_evidence(vm, guest, report, error, symbols, diagnostics=False)
                raise
            finally:
                guest.close()
                report['source_disk_unchanged'] = sha(data) == report['source_sha256']
                (vm.out/'curl.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
    print(report['status'], flush=True)


if __name__ == '__main__':
    main()
