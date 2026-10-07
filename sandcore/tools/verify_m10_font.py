#!/usr/bin/env python3
"""全量原生字形实际客体字节与独立FreeType对照，不宣称Notes/坏字体完成。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import traceback

from font_coverage import unicode_mapping
from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm
from verify_m10_lifecycle import run_case
from m10_guest_boot import unique_symbols, wait_first_desktop
from m10_verification_report import checkpoint

ROOT = Path(__file__).resolve().parents[1]


def linux_path(path):
    absolute=path.resolve()
    if len(absolute.drive)!=2 or absolute.drive[1]!=':':
        raise ValueError('WSL参考只接受明确的本机盘符路径')
    return '/mnt/'+absolute.drive[0].lower()+absolute.as_posix()[2:]


def unhinted_reference(blob,font_path,directory,face):
    actual=directory/f'native-complete-{face}.bin'
    with actual.open('xb') as output: output.write(blob)
    destination=directory/f'freetype-original-{face}.json'
    command=['wsl.exe','python3',linux_path(ROOT/'tools/font_reference_freetype.py'),
             '--font',linux_path(font_path),'--dump',linux_path(actual),'--out',linux_path(destination)]
    run=subprocess.run(command,capture_output=True,encoding='utf-8',errors='replace',timeout=90)
    (directory/f'freetype-original-{face}-run.json').write_text(json.dumps(dict(command=command,
        exit_code=run.returncode,stdout=run.stdout,stderr=run.stderr),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    result=json.loads(destination.read_text(encoding='utf-8'))
    if (result['dump_sha256']!=hashlib.sha256(blob).hexdigest() or result['font_sha256']!=sha(font_path)
            or result['face']!=face or result['reference']['hinting']):
        raise AssertionError('独立参考正文/原字体/字号/无hint合同不符')
    if run.returncode not in (0,1) or (run.returncode==0)!=(result['status']=='ALL_NATIVE_GLYPH_PIXELS_MATCH'):
        raise RuntimeError('独立FreeType参考执行状态不一致')
    return result


def font_metadata(raw):
    tables = {raw[12+index*16:16+index*16].decode('ascii'):struct.unpack_from('>II',raw,20+index*16)
              for index in range(struct.unpack_from('>H',raw,4)[0])}
    units = struct.unpack_from('>H',raw,tables['head'][0]+18)[0]
    ascent,descent,gap = struct.unpack_from('>hhh',raw,tables['hhea'][0]+4)
    metrics = struct.unpack_from('>H',raw,tables['hhea'][0]+34)[0]
    return tables,units,ascent,descent,gap,metrics


def validate_dump(blob,font_path,face,mapping,metadata):
    from PIL import Image, ImageDraw, ImageFont, __version__
    from PIL import features
    raw = font_path.read_bytes()
    tables,units,ascent,descent,gap,metrics = font_metadata(raw)
    if len(blob)<80 or struct.unpack_from('<4I',blob)!=(0x31464D53,1,face,len(mapping)):
        raise AssertionError('实际字体转储头不符')
    info = struct.unpack_from('<16I',blob,16)
    signed = lambda value: value if value<0x80000000 else value-0x100000000
    if (info[0]!=1 or info[2]!=face or info[3]!=1 or info[4]!=metadata['glyphs']
            or info[5]!=len(mapping) or info[6]!=units or signed(info[7])!=ascent
            or signed(info[8])!=descent or signed(info[9])!=gap or info[10]!=len(raw) or info[15]!=metrics):
        raise AssertionError('客体激活脸/完整统计/原字节与独立原TTF元数据不符')
    font = ImageFont.truetype(str(font_path),face,layout_engine=ImageFont.Layout.BASIC)
    at,checked,mismatches = 80,0,[]
    for scalar,expected_id in sorted(mapping.items()):
        if at+72>len(blob):
            raise AssertionError('真实转储记录截断')
        actual_scalar,returned = struct.unpack_from('<2I',blob,at)
        head = struct.unpack_from('<16I',blob,at+8)
        at += 72
        width,height,baseline,left = head[6],head[7],head[8],signed(head[9])
        if actual_scalar!=scalar or head[0]!=1 or head[1]!=info[1] or head[2]!=expected_id or head[3]!=1 or head[4]!=face or head[10]!=face:
            raise AssertionError('客体实际字符/编号/脸/代数不符')
        metric_at = tables['hmtx'][0]+min(expected_id,metrics-1)*4
        advance = struct.unpack_from('>H',raw,metric_at)[0]
        bearing_at = (tables['hmtx'][0]+expected_id*4+2 if expected_id<metrics else
                      tables['hmtx'][0]+metrics*4+(expected_id-metrics)*2)
        bearing = struct.unpack_from('>h',raw,bearing_at)[0]
        # 这些两份原点阵体原生网格恰为整数像素。若元数据改变成
        # 分数网格就明确失败，不用相同扫描算法充当独立参考实现。
        if (advance*face)%units or (bearing*face*64)%units or (ascent*face)%units or (-descent*face)%units:
            raise AssertionError('原字体原生网格改变，需要独立参考合同')
        expected_advance=advance*face//units
        if (returned!=expected_advance or head[5]!=expected_advance or baseline!=ascent*face//units
                or signed(head[11])!=bearing*face*64//units or head[12]!=expected_advance*64
                or height!=(ascent-descent)*face//units or not 0<=width<=128 or not 1<=height<=96 or any(head[13:])):
            raise AssertionError('客体前进/轴承/基线/行框/保留字不符')
        words=(width+31)//32
        amount=height*words*4
        if at+amount>len(blob):
            raise AssertionError('真实位图记录截断')
        rows=struct.unpack_from('<'+str(height*words)+'I',blob,at) if words else ()
        at += amount
        actual=bytes(255 if rows[y*words+x//32]&(1<<(31-x%32)) else 0 for y in range(height) for x in range(width))
        reference=Image.new('L',(width,height),0)
        # 基线锚ls直接对应原TTF字形笔尖；用FreeType的1bit字体模式
        # 独立绘制，避免自己复制内核边交点排序/非零绕数算法做自证。
        drawing=ImageDraw.Draw(reference);drawing.fontmode='1'
        drawing.text((-left,baseline),chr(scalar),font=font,fill=255,anchor='ls')
        expected=reference.tobytes()
        if actual!=expected:
            mismatches.append(dict(scalar=f'U+{scalar:04X}',glyph=expected_id,width=width,height=height,
                                   actual_sha256=hashlib.sha256(actual).hexdigest(),
                                   freetype_sha256=hashlib.sha256(expected).hexdigest(),
                                   different_pixels=sum(a!=b for a,b in zip(actual,expected))))
        checked += 1
    if at!=len(blob):
        raise AssertionError('转储有额外/伪造记录或尾字节')
    return dict(status='ALL_NATIVE_GLYPH_PIXELS_MATCH' if not mismatches else 'NATIVE_PIXEL_MISMATCH',
                face=face,mapped_checked=checked,glyphs_declared=metadata['glyphs'],dump_sha256=hashlib.sha256(blob).hexdigest(),
                font_sha256=sha(font_path),font_info=list(info),mismatches=mismatches,
                independent_reference=dict(renderer='Pillow FreeType 1bit baseline anchor ls BASIC',
                                           pillow=__version__,freetype=features.version_module('freetype2')))


def run_disk(boot,data,directory,fonts,symbols):
    report=dict(status='RUNNING',scope='ALL_ORIGINAL_MAPPED_NATIVE_GLYPHS_ONLY',cases=[],fonts=[],screenshots=[])
    with QemuSession(boot,data,directory,'tcg',True,audio=False,network='none',memory=256) as vm:
        guest=Guest(vm)
        try:
            guest.wait(rb'CORE START SYS/CORE/CORE.SKM\r\n',timeout=30,debug=True)
            wait_first_desktop(guest,symbols,report)
            guest.connect()
            source=(ROOT/'tests/m10/M10FONT.C').read_bytes()
            guest.put_bytes(source,'/TMP/M10FONT.C','font-probe-source')
            report['fixture_source_sha256']=hashlib.sha256(source).hexdigest()
            run_case(guest,report,'native-full-font-fixture','s3c /TMP/M10FONT.C /TMP/M10FONT.SCX',timeout=360)
            for face,path,mapping,metadata in fonts:
                map_blob=b''.join(struct.pack('<2I',scalar,glyph) for scalar,glyph in sorted(mapping.items()))
                guest.put_bytes(map_blob,f'/TMP/M10FM{face}.BIN',f'original-map-{face}')
                report.setdefault('mapping_inputs',[]).append(dict(face=face,sha256=hashlib.sha256(map_blob).hexdigest(),scalars=len(mapping)))
                run_case(guest,report,f'all-{len(mapping)}-native-{face}px-and-ABI-bounds',
                         f'/TMP/M10FONT.SCX {face} /TMP/M10FM{face}.BIN /TMP/M10FB{face}.BIN',
                         (b'PASS every original Unicode mapping',b'PASS bitmap/info boundaries'),timeout=900)
                actual=guest.get_bytes(f'/TMP/M10FB{face}.BIN',f'native-bitmap-{face}')
                result=validate_dump(actual,path,face,mapping,metadata)
                # 原合同保留轮廓、不运行hint或自动调形。Pillow默认开启
                # hint的结果单独保留作为差异诊断，不能拿它错误要求内核
                # 移动原文件中的重音轮廓；正式逐像素参考明确禁用hint。
                diagnostic=dict(status=result['status'],mismatches=result['mismatches'],
                                independent_reference=result['independent_reference'])
                reference=unhinted_reference(actual,path,vm.out,face)
                if reference['mapped_checked']!=len(mapping):
                    raise AssertionError('独立参考没有覆盖完整映射')
                result.update(status=reference['status'],mismatches=reference['mismatches'],
                              independent_reference=reference['reference'],hinted_diagnostic=diagnostic,
                              actual_pixels_sha256=reference['actual_pixels_sha256'],
                              reference_pixels_sha256=reference['reference_pixels_sha256'])
                report['fonts'].append(result)
                if result['status']!='ALL_NATIVE_GLYPH_PIXELS_MATCH':
                    raise AssertionError(f'{face}px actual native glyphs differ from independent FreeType')
                checkpoint(vm.out/'font.json',report)
                print(f'{face}px/{len(mapping)} real glyphs: FreeType exact pixels PASS',flush=True)
            vm.hmp('sendkey esc')
            picture=vm.out/'font-probe-desktop.ppm'
            vm.hmp('screendump "'+picture.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(picture))
            if 'ata_diagnostic' in symbols:
                report['storage_counters']=vm.hmp(f"xp /16wx 0x{symbols['ata_diagnostic']:08x}")
            report['status']='DECLARED_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,message=str(error),traceback=traceback.format_exc()))
            try:
                if vm.process.poll() is None:
                    vm.qmp('stop')
                    report['failure_cpu_registers']=vm.hmp('info registers')
                    report['failure_storage_counters']={name:vm.hmp(f'xp /{words}wx 0x{symbols[name]:08x}')
                                                        for name,words in (('fs_faulted',1),('ata_diagnostic',16)) if name in symbols}
                    picture=vm.out/'font-failure.ppm';vm.hmp('screendump "'+picture.as_posix()+'"')
                    report['screenshots'].append(png_from_ppm(picture))
            except Exception as capture_error:
                report['capture_error']=str(capture_error)
            raise
        finally:
            try:
                guest.close()
            finally:
                report['source_unchanged']=sha(data)==vm.report['sources']['sanddata']['sha256']
                checkpoint(vm.out/'font.json',report)
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot',type=Path,required=True)
    parser.add_argument('--data',type=Path,action='append',required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--symbols',type=Path,required=True)
    args=parser.parse_args()
    if os.name!='nt' or len(args.data)<2 or len(set(path.resolve(strict=True) for path in args.data))!=len(args.data):
        parser.error('Windows Python与两不同来源盘')
    fonts=[]
    symbols=unique_symbols(args.symbols)
    for face in (12,16):
        path=ROOT/f'third_party/vonwaon/VonwaonBitmap-{face}px.ttf'
        mapping,metadata=unicode_mapping(path);fonts.append((face,path,mapping,metadata))
    args.out.mkdir(parents=True,exist_ok=False)
    report=dict(status='RUNNING',scope='ALL_NATIVE_MAPPING_PIXELS_NOT_WHOLE_FONT_NOTES_CONTRACT',disks=[],
                remaining=['bad-font rejection and recovery','scaled/fallback/legacy old SCX glyph ABI',
                           'Notes Chinese caret selection scrolling and real layout screenshots','OOM/cache generation/lifecycle'])
    checkpoint(args.out/'font-matrix.json',report)
    try:
        for index,data in enumerate(args.data,1):
            report['disks'].append(run_disk(args.boot,data,args.out/f'disk-{index}',fonts,symbols))
            checkpoint(args.out/'font-matrix.json',report)
        report['status']='DECLARED_CASES_PASS'
    except BaseException as error:
        report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,message=str(error),traceback=traceback.format_exc()))
        raise
    finally:
        checkpoint(args.out/'font-matrix.json',report)


if __name__=='__main__':
    main()
