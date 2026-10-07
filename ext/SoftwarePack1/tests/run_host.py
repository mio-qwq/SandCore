"""Offline tests of the real application sources; never boots SandCore.
Only a private build/host copy of the syscall bridge is replaced by mocks.
"""
from pathlib import Path
import json, re, shutil, subprocess, sys
ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / 'build/host'
LIB = HOST / 'libex'
HOST.mkdir(parents=True, exist_ok=True)
shutil.copytree(ROOT.parent / 'libex', LIB, dirs_exist_ok=True)
p = LIB / 'SCAPI.H'
src = p.read_text(encoding='utf-8-sig')
start = src.index('static inline int sc_call(')
end = src.index('\n/* w/h ', start)
src = src[:start] + '''#include <stdint.h>
intptr_t host_call(intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t,intptr_t);
static inline intptr_t sc_call(intptr_t n,intptr_t a,intptr_t b,intptr_t c,intptr_t d,intptr_t e,intptr_t f)
{return host_call(n,a,b,c,d,e,f);}
''' + src[end:]
src = src.replace('(int)', '(intptr_t)')
p.write_text(src, encoding='utf-8')
# Use the target's actual Phoenix ASCII glyphs in source-rendered previews.
data = json.loads((ROOT.parents[1] / 'sandcore/assets/font/phoenix-ascii.json').read_text(encoding='utf-8'))
rows = data['rows16']
font = 'static const unsigned short host_font[95][16] = {\n'
font += ',\n'.join('{' + ','.join(map(str, r)) + '}' for r in rows) + '\n};\n'
(HOST / 'font_rows.h').write_text(font)
for name in (sys.argv[1:] or ('util', 'sheet', 'blocks', 'hex', 'raider', 'installer')):
    out = HOST / (name + '_test')
    args = ['gcc', '-std=c99', '-D__SCCC__=1', '-g', '-O1', '-fno-omit-frame-pointer',
            '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-Wno-pointer-to-int-cast',
            '-I'+str(LIB), '-I'+str(HOST), str(ROOT/'tests'/ (name+'_test.c')),
            str(LIB/'exutil.c'), str(LIB/'exaudio.c'), '-o', str(out)]
    if name == 'installer': args.insert(-2, str(ROOT/'installer/payload.c'))
    subprocess.run(args, check=True)
    subprocess.run([str(out)], check=True, cwd=ROOT,
                   env={**__import__('os').environ, 'ASAN_OPTIONS':'detect_leaks=0'},timeout=30)
