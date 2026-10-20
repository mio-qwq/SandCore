"""Copyright (c) 2026 mio. Exercise SBNET itself over local HTTP/TLS sockets.

The TLS fixture CA is appended only to a separate host test executable. Shipped
anchors are never changed. Everything created by this test stays in build/.
"""
from pathlib import Path
import gzip
import importlib.util
import json
import os
import re
import socketserver
import ssl
import struct
import subprocess
import threading
import zlib

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / 'build/network-regression'
BODY = b'Hello from the real SBNET engine.\n'


def run(command, **kwargs):
    result = subprocess.run(command, cwd=ROOT, capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(result.stderr.decode('utf-8', 'replace')[-6000:])
    return result.stdout


def build_probe(name, extra=()):
    output = 'build/' + name
    flags = ['gcc', '-Os', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
             '-Iinclude', '-I../sandcore/user', '-Ivendor/bearssl-0.6/inc',
             '-Ivendor/miniz-3.0.2', '-Ibuild/generated', '-Ibuild/network-regression',
             '-DMINIZ_NO_STDIO', '-DMINIZ_NO_ARCHIVE_APIS', '-DMINIZ_NO_ZLIB_APIS']
    run(flags + list(extra) + ['tools/net_host_probe.c', 'src/net.c', 'src/common.c',
        'src/entropy.c', 'src/url.c', 'vendor/miniz-3.0.2/miniz_tinfl.c',
        'build/host-bearssl/libbearssl.a', '-o', output])
    return ROOT / output


def tls_fixture():
    ca, key, cert, leaf = [WORK / x for x in ('ca.pem', 'ca.key', 'server.pem', 'server.key')]
    if not cert.exists():
        run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-sha256',
             '-days', '3650', '-subj', '/CN=SandBrowser regression CA', '-addext',
             'basicConstraints=critical,CA:TRUE', '-addext', 'keyUsage=critical,keyCertSign,cRLSign',
             '-keyout', str(key), '-out', str(ca)])
        run(['openssl', 'req', '-new', '-newkey', 'rsa:2048', '-nodes',
             '-subj', '/CN=localhost', '-keyout', str(leaf), '-out', str(WORK / 'server.csr')])
        (WORK / 'server.ext').write_text('subjectAltName=DNS:localhost\nbasicConstraints=critical,CA:FALSE\n'
                                        'keyUsage=critical,digitalSignature,keyEncipherment\n'
                                        'extendedKeyUsage=serverAuth\n')
        run(['openssl', 'x509', '-req', '-in', str(WORK / 'server.csr'), '-CA', str(ca),
             '-CAkey', str(key), '-CAcreateserial', '-days', '365', '-sha256',
             '-extfile', str(WORK / 'server.ext'), '-out', str(cert)])
    spec = importlib.util.spec_from_file_location('sb_anchors', ROOT / 'tools/make_anchors.py')
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    der = ssl.PEM_cert_to_DER_cert(ca.read_text())
    tlvs = generator.tlvs
    tbs = tlvs(tlvs(tlvs(der)[0][1])[0][1])
    if tbs[0][0] == 0xa0: tbs = tbs[1:]
    dn = tbs[4][2]
    key_tlv = tlvs(tlvs(tbs[5][1])[1][1][1:])[0][1]
    integers = tlvs(key_tlv)
    modulus, exponent = [x[1].lstrip(b'\0') for x in integers]
    declarations = ''.join('static const unsigned char fixture_' + name + '[]={' +
                           ','.join(str(x) for x in value) + '};\n'
                           for name, value in (('dn', dn), ('n', modulus), ('e', exponent)))
    entry = '{{(unsigned char*)fixture_dn,sizeof(fixture_dn)},BR_X509_TA_CA,' \
            '{BR_KEYTYPE_RSA,{.rsa={(unsigned char*)fixture_n,sizeof(fixture_n),' \
            '(unsigned char*)fixture_e,sizeof(fixture_e)}}}},\n'
    header = (ROOT / 'build/generated/anchors.h').read_text()
    header = declarations + header.replace('};\n#define SB_ANCHOR_COUNT', entry + '};\n#define SB_ANCHOR_COUNT')
    (WORK / 'fixture-anchors.h').write_text(header)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert, leaf)
    return context


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True
    tls = None


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        sock = self.request
        sock.settimeout(3)
        try:
            if self.server.tls: sock = self.server.tls.wrap_socket(sock, server_side=True)
            data = b''
            while b'\r\n\r\n' not in data:
                part = sock.recv(4096)
                if not part: return
                data += part
                if len(data) > 65536: return
            head, body = data.split(b'\r\n\r\n', 1)
            line = head.split(b'\r\n', 1)[0]
            method, target, _ = line.decode('ascii').split(' ', 2)
            match = re.search(br'(?im)^Content-Length: *(\d+)', head)
            size = int(match[1]) if match else 0
            while len(body) < size:
                part = sock.recv(size - len(body))
                if not part: return
                body += part
            path = target.split('?', 1)[0]
            headers, content, status = {}, BODY, '200 OK'
            if path == '/echo': content = method.encode() + b':' + body
            elif path == '/space%20name': content = target.encode()
            elif path == '/gzip': content = gzip.compress(BODY); headers['Content-Encoding'] = 'gzip'
            elif path == '/gzip-empty': content = gzip.compress(b''); headers['Content-Encoding'] = 'gzip'
            elif path == '/gzip-concat': content = gzip.compress(BODY[:8]) + gzip.compress(BODY[8:]); headers['Content-Encoding'] = 'gzip'
            elif path == '/gzip-header-crc':
                content = bytearray(gzip.compress(BODY)); content[3] |= 2
                content = bytes(content[:10]) + struct.pack('<H', zlib.crc32(content[:10]) & 65535) + bytes(content[10:])
                headers['Content-Encoding'] = 'gzip'
            elif path == '/zlib': content = zlib.compress(BODY); headers['Content-Encoding'] = 'deflate'
            elif path == '/zlib-empty': content = zlib.compress(b''); headers['Content-Encoding'] = 'deflate'
            elif path == '/raw-deflate': content = zlib.compress(BODY)[2:-4]; headers['Content-Encoding'] = 'deflate'
            elif path == '/large-gzip': content = gzip.compress(b'A' * 200000); headers['Content-Encoding'] = 'gzip'
            elif path == '/bad-gzip': content = gzip.compress(BODY)[:-8] + b'\0' * 8; headers['Content-Encoding'] = 'gzip'
            elif path == '/bad-zlib': content = zlib.compress(BODY)[:-4] + b'\0' * 4; headers['Content-Encoding'] = 'deflate'
            elif path == '/encoding': content = b'not negotiated'; headers['Content-Encoding'] = 'br'
            elif path == '/short-body': headers['Content-Length'] = '999'
            elif path == '/length-list': headers['Content-Length'] = str(len(content)) + ', ' + str(len(content))
            elif path == '/bad-length': headers['Content-Length'] = '3, 4'
            elif path == '/large-length': headers['Content-Length'] = '16777217'
            elif path == '/bad-status': sock.sendall(b'NOT HTTP\r\n\r\n'); return
            elif path == '/bad-headers': sock.sendall(b'HTTP/1.1 200 OK\r\nInvalid header\r\n\r\n'); return
            elif path == '/bad-redirect': status = '302 Found'; headers['Location'] = 'file:/HOME/SECRET'
            elif path in ('/relative', '/post303', '/post307', '/downgrade'):
                status = '307 Temporary Redirect' if path == '/post307' else '303 See Other' if path == '/post303' else '302 Found'
                headers['Location'] = './echo' if path.startswith('/post') else './ok'
                if path == '/downgrade':
                    headers['Location'] = 'http://localhost:' + str(plain.server_address[1]) + '/cookie-check'
                    headers['Set-Cookie'] = 'secret=secure; Secure; Path=/'
            elif path.startswith('/chain/'):
                count = int(path.rsplit('/', 1)[1])
                if count: status = '302 Found'; headers['Location'] = '/chain/' + str(count - 1)
            elif path == '/cookie-check' and b'secret=secure' in head: content = b'SECURE COOKIE LEAK'
            if path in ('/chunked', '/bad-chunk'):
                sock.sendall(b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n')
                sock.sendall((b'%x ; test="x"\r\n' % len(content)) + content + b'\r\n')
                if path == '/chunked': sock.sendall(b'0\r\nX-Trailer: end\r\n\r\n')
                return
            if path != '/close': headers.setdefault('Content-Length', str(len(content)))
            headers['Connection'] = 'close'
            wire = ('HTTP/1.1 ' + status + '\r\n' + ''.join(k + ': ' + v + '\r\n' for k, v in headers.items()) + '\r\n').encode()
            sock.sendall(wire + (b'' if method == 'HEAD' else content))
        except (OSError, ssl.SSLError, ValueError):
            pass
        finally:
            sock.close()  # Deliberately no TLS close_notify for the /close fixture.


def fnv(data):
    value = 2166136261
    for byte in data: value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def probe(url, *, error=0, body=BODY, secure=0, flags=0, method='GET', post='', fault=None,
          executable=None, version='v2'):
    environment = os.environ.copy()
    if fault: environment['SB_HOST_FAULT'] = fault
    command = [str(executable or trusted), url, str(flags), method, post, version]
    result = json.loads(run(command, timeout=25, env=environment))
    assert result['error'] == error, (url, result, 'expected error', error)
    if not error:
        assert result['result'] == 0 and result['status'] == 200 and result['secure'] == secure, (url, result)
        assert result['bytes'] == len(body) and result['hash'] == fnv(body), (url, result)
    return result


def main():
    global plain, trusted
    WORK.mkdir(parents=True, exist_ok=True)
    context = tls_fixture()
    production = build_probe('net_host_probe')
    trusted = build_probe('net_fixture_probe', ['-DSB_TRUST_HEADER="fixture-anchors.h"'])
    plain = Server(('127.0.0.1', 0), Handler)
    encrypted = Server(('127.0.0.1', 0), Handler); encrypted.tls = context
    for server in (plain, encrypted): threading.Thread(target=server.serve_forever, daemon=True).start()
    http = 'http://localhost:' + str(plain.server_address[1])
    https = 'https://localhost:' + str(encrypted.server_address[1])
    checks = []
    try:
        for path in ('ok', 'gzip', 'gzip-concat', 'gzip-header-crc', 'zlib', 'raw-deflate', 'chunked', 'close', 'length-list', 'relative', 'chain/20'):
            probe(http + '/' + path); checks.append(path)
        for path in ('gzip-empty', 'zlib-empty'): probe(http + '/' + path, body=b''); checks.append(path)
        probe(http + '/large-gzip', body=b'A' * 200000); checks.append('bounded inflate growth')
        probe(http + '/space name', body=b'/space%20name'); checks.append('URL spaces')
        probe(http + '/ok', body=b'', method='HEAD'); checks.append('HEAD')
        probe(http + '/post303', method='POST', post='a=1', body=b'GET:'); checks.append('303 POST becomes GET')
        probe(http + '/post307', method='POST', post='a=1', body=b'POST:a=1'); checks.append('307 preserves POST')
        probe(http + '/echo', method='POST', body=b'POST:'); checks.append('empty POST')
        probe(http + '/ok', version='v1'); checks.append('v1 ABI')
        for path, code in (('short-body', -20), ('bad-chunk', -20), ('bad-gzip', -21), ('bad-zlib', -21),
                           ('bad-length', -16), ('large-length', -16), ('bad-status', -14),
                           ('bad-headers', -15), ('bad-redirect', -18), ('encoding', -17), ('chain/21', -22)):
            probe(http + '/' + path, error=code); checks.append(path)
        for fault, code in (('dns', -1), ('socket', -2), ('connect', -3), ('write', -13), ('read', -14),
                            ('record', -10), ('body', -10), ('header', -12)):
            target = http.replace('localhost', 'network-test.invalid') if fault == 'dns' else http
            probe(target + '/ok', fault=fault, error=code); checks.append('fault: ' + fault)
        probe(http + '/ok', flags=0x8000, error=-10); checks.append('unknown request flag')
        probe('not a URL', error=-11); checks.append('bad URL')
        probe(https + '/ok', secure=1); checks.append('TLS verified response')
        probe(https + '/close', secure=1); checks.append('TLS HTTP close-delimited EOF')
        probe(https + '/short-body', error=-20); checks.append('TLS explicit length still required')
        probe(https + '/downgrade'); checks.append('top-level HTTPS to HTTP and secure Cookie isolation')
        probe(https + '/downgrade', flags=1, error=-19); checks.append('HTTPS subresource redirect policy')
        result = probe(https + '/ok', executable=production, error=-13)
        assert result['tls'] == 62, result
        checks.append('untrusted fixture CA rejected by shipped roots')
        result = probe(https.replace('localhost', '127.0.0.1') + '/ok', error=-13)
        assert result['tls'] == 56, result
        checks.append('certificate hostname verification')
    finally:
        plain.shutdown(); encrypted.shutdown(); plain.server_close(); encrypted.server_close()
    (WORK / 'results.json').write_text(json.dumps({'passed': len(checks), 'checks': checks}, indent=2))
    print('PASS:', len(checks), 'SBNET HTTP/TLS response, redirect, compression and failure checks.')


if __name__ == '__main__': main()
