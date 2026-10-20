"""Copyright (c) 2026 mio. Build the two-file ZIP and record exact shipped hashes."""
from pathlib import Path
import binascii
import hashlib
import html
import json
import math
import os
import re
import struct
import sys
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.search(r'#define SB_VERSION "([^"]+)"',
                    (ROOT / 'include/sb.h').read_text()).group(1)
os.chdir(ROOT)
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT.parent / 'sandcore/tools'))
import scl_format
import scd_format


def digest(data):
    return hashlib.sha256(data).hexdigest()


def artwork():
    """Original code-drawn globe; no Microsoft assets or external images."""
    width, height = 160, 120
    pixels = bytearray()
    for y in range(height - 1, -1, -1):
        for x in range(width):
            xx, yy = (x - 80) / 52, (y - 58) / 52
            rr = xx * xx + yy * yy
            rgb = (220, 232, 241)
            if rr <= 1:
                shade = max(0.25, (1 - rr) ** 0.5 * 0.6 + 0.4 - xx * 0.15)
                rgb = (int(30 * shade), int(100 * shade), int(190 * shade))
                longitude = math.atan2(xx, max(0, 1 - rr) ** 0.5)
                latitude = math.asin(yy)
                if abs(math.sin(longitude * 5)) < 0.055 or abs(math.sin(latitude * 6)) < 0.06:
                    rgb = (125, 180, 220)
                land = math.sin(longitude * 3 + 0.7) + math.cos(latitude * 5 + longitude) + 0.4 * math.sin(latitude * 9)
                if land > 1.1:
                    rgb = (int(80 * shade), int(155 * shade), int(90 * shade))
            pixels.extend(reversed(rgb))
    header = b'BM' + struct.pack('<IHHI', 54 + len(pixels), 0, 0, 54)
    header += struct.pack('<IiiHHIIiiII', 40, width, height, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
    (ROOT / 'resources/WORLD.BMP').write_bytes(header + pixels)


def notices():
    parts = [
        f'SandBrowser {VERSION}\nCopyright (c) 2026 mio. Original application code: all rights reserved.\n',
        'Independent browser shell, platform adaptation, IPC protocol, installer and original graphics by mio.\n',
        'Third-party code retains its original authors and licenses. No Microsoft artwork or browser code is included.\n',
        'litehtml 0.9 / Yuri Kobets: BSD-3-Clause. Bundled Gumbo: Copyright 2010 Google Inc. All Rights Reserved. '
        'Author Jonathan Tang (jdtang@google.com). Apache-2.0.\n',
        'Duktape 2.7.0 / its authors: MIT. BearSSL 0.6 / Thomas Pornin: MIT.\n',
        'miniz 3.0.2: MIT. musl 1.2.5: MIT and the notices below. '
        'stb_image and stb_sprintf 1.10: MIT alternative selected. '
        'stb_sprintf originally by Jeff Roberts / RAD Game Tools, with the credited stb contributors.\n',
        'GCC 15 runtime/header portions: GPL-3.0-or-later WITH GCC-exception-3.1. '
        'Built by GCC using an eligible compilation process; the exception permits independent non-GPL application modules.\n',
        'Chromium trust snapshot: Chromium BSD notice below. Certificates retain their issuers; '
        'the snapshot is not an endorsement and is not the full Chrome trust policy.\n',
        'Local litehtml modifications by mio: single-thread configuration; remove unused iostream dependencies; '
        'URL string concatenation; diagnostic name without RTTI; DOM render-tree rebuild; fragment parsing.\n',
    ]
    files = [
        'vendor/litehtml-0.9/LICENSE', 'vendor/litehtml-0.9/src/gumbo/LICENSE',
        'vendor/duktape-2.7.0/LICENSE.txt', 'vendor/duktape-2.7.0/AUTHORS.rst',
        'vendor/bearssl-0.6/LICENSE.txt', 'vendor/miniz-3.0.2/LICENSE',
        'vendor/musl-1.2.5/COPYRIGHT', 'vendor/stb-master/LICENSE',
        'resources/licenses/CHROMIUM.TXT', 'resources/licenses/GCC-EXCEPTION.TXT',
        'resources/licenses/GPL-3.TXT',
    ]
    for name in files:
        content = (ROOT / name).read_text(encoding='utf-8')
        if len(content) < 100:
            raise ValueError('Missing license text: ' + name)
        parts.append('\n' + '=' * 72 + '\n' + name + '\n' + '=' * 72 + '\n' + content)
    # Several litehtml files carry additional BSD/UC/Sun notices independent of LICENSE.
    seen = set()
    for source in sorted((ROOT / 'vendor/litehtml-0.9/src').glob('*.cpp')):
        text = source.read_text(encoding='utf-8')
        prefix = text.split('#include', 1)[0]
        if ('Copyright' in prefix or 'copyright' in prefix) and prefix not in seen:
            seen.add(prefix)
            parts.append('\nAdditional source notice: ' + source.name + '\n' + prefix)
    for source in sorted((ROOT / 'vendor/musl-1.2.5/src/math').glob('*.c')):
        prefix = source.read_text(encoding='utf-8').split('#include', 1)[0]
        if ('Copyright' in prefix or 'copyright' in prefix) and prefix not in seen:
            seen.add(prefix)
            parts.append('\nMath source notice: ' + source.name + '\n' + prefix)
    for name in ('stl_tree.h', 'stl_list.h', 'hashtable.h', 'hash_bytes.h', 'basic_string.h'):
        source = Path('/usr/include/c++/15/bits') / name
        prefix = source.read_text(encoding='utf-8').split('#ifndef', 1)[0]
        if prefix not in seen:
            seen.add(prefix)
            parts.append('\nGCC runtime/header source notice: ' + name + '\n' + prefix)
    result = '\n'.join(parts)
    (ROOT / 'resources/NOTICE.TXT').write_text(result, encoding='utf-8')
    page = '<!doctype html><html><head><meta charset="utf-8"><title>Component licenses</title></head><body style="font:15px Tahoma;margin:24px"><h1>Component credits and licenses</h1><p>Author of SandBrowser: <b>mio</b>. Third-party authors retain their copyrights.</p><p><a href="about:about">About</a> | <a href="about:help">Guide</a></p><pre style="white-space:pre-wrap">' + html.escape(result) + '</pre></body></html>'
    (ROOT / 'resources/LICENSES.HTM').write_text(page, encoding='utf-8')


def main():
    artwork()
    notices()
    payload = ROOT / 'build/payload'
    shipped = {p.name: p.read_bytes() for p in payload.glob('*') if p.suffix in ('.SCX', '.SCL')}
    required = {'BROWSER.SCX', 'WEBPAGE.SCX', 'WEBNET.SCX', 'SBUI.SCL', 'SBPAGE.SCL', 'SBNET.SCL'}
    if set(shipped) != required:
        raise ValueError('The six runtime components must be built together')
    for name, data in shipped.items():
        if name.endswith('.SCX'):
            scd_format.validate_scx(data)
        else:
            scl_format.validate(data)
    for name in ('WELCOME.HTM', 'HELP.HTM', 'ABOUT.HTM', 'LAB.HTM', 'LICENSES.HTM', 'NOTICE.TXT', 'WORLD.BMP'):
        shipped[name] = (ROOT / 'resources' / name).read_bytes()
    shipped['ROOTS.JSON'] = (ROOT / 'build/anchors.json').read_bytes()
    manifest = {
        'product': 'SandBrowser', 'version': VERSION, 'author': 'mio',
        'guest_execution': 'not run; reserved for the user',
        'files': [{'name': name, 'bytes': len(data), 'sha256': digest(data)} for name, data in sorted(shipped.items())],
    }
    shipped['MANIFEST.JSON'] = json.dumps(manifest, ensure_ascii=False, indent=2).encode('utf-8')
    total = sum(map(len, shipped.values()))
    archive = bytearray(b'SBPACK1\0' + struct.pack('<III', 1, len(shipped), total))
    for name, data in sorted(shipped.items()):
        packed = zlib.compress(data, 9)
        archive.extend(name.encode('ascii').ljust(32, b'\0'))
        archive.extend(struct.pack('<III', len(data), len(packed), binascii.crc32(data) & 0xffffffff))
        archive.extend(packed)
    (ROOT / 'dist/BROWSER.PAK').write_bytes(archive)
    setup = (ROOT / 'dist/SETUP.SCX').read_bytes()
    scd_format.validate_scx(setup)
    path = ROOT / 'dist' / f'SandBrowser-{VERSION}.zip'
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for name, data in [('SETUP.SCX', setup), ('BROWSER.PAK', archive)]:
            info = zipfile.ZipInfo(name, (2026, 10, 20, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            zf.writestr(info, data)
    manifest['package'] = {'name': path.name, 'bytes': path.stat().st_size, 'sha256': digest(path.read_bytes())}
    manifest['installer'] = {'bytes': len(setup), 'sha256': digest(setup)}
    manifest['payload'] = {'bytes': len(archive), 'unpacked_bytes': total, 'sha256': digest(archive)}
    (ROOT / 'dist/BUILD.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print(path.name, path.stat().st_size, 'bytes;', len(shipped), 'installed files')
    print('SCX/SCL container validation passed. No guest execution was performed.')


if __name__ == '__main__':
    main()
