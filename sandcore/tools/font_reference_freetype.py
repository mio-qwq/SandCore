#!/usr/bin/env python3
"""WSL已安装FreeType直接无hint栅格参考；不向客体复制第三方引擎。"""
import argparse
import ctypes as C
from ctypes.util import find_library
import hashlib
import json
from pathlib import Path
import struct
import sys


# 公共C ABI对应FreeType官方FaceRec/GlyphSlotRec/Bitmap定义，仅读到
# glyph位图/轴承字段。不访问引擎私有结构，也不复用SandCore扫描算法。
class Generic(C.Structure):
    _fields_=[('data',C.c_void_p),('finalizer',C.c_void_p)]


class Vector(C.Structure):
    _fields_=[('x',C.c_long),('y',C.c_long)]


class Box(C.Structure):
    _fields_=[(name,C.c_long) for name in ('xMin','yMin','xMax','yMax')]


class Metrics(C.Structure):
    _fields_=[(name,C.c_long) for name in ('width','height','horiBearingX','horiBearingY',
                                        'horiAdvance','vertBearingX','vertBearingY','vertAdvance')]


class Bitmap(C.Structure):
    _fields_=[('rows',C.c_uint),('width',C.c_uint),('pitch',C.c_int),('buffer',C.POINTER(C.c_ubyte)),
              ('num_grays',C.c_ushort),('pixel_mode',C.c_ubyte),('palette_mode',C.c_ubyte),('palette',C.c_void_p)]


class Slot(C.Structure):
    _fields_=[('library',C.c_void_p),('face',C.c_void_p),('next',C.c_void_p),('glyph_index',C.c_uint),
              ('generic',Generic),('metrics',Metrics),('linearHoriAdvance',C.c_long),
              ('linearVertAdvance',C.c_long),('advance',Vector),('format',C.c_int),
              ('bitmap',Bitmap),('bitmap_left',C.c_int),('bitmap_top',C.c_int)]


class Face(C.Structure):
    _fields_=[(name,C.c_long) for name in ('num_faces','face_index','face_flags','style_flags','num_glyphs')]+[
        ('family_name',C.c_char_p),('style_name',C.c_char_p),('num_fixed_sizes',C.c_int),('available_sizes',C.c_void_p),
        ('num_charmaps',C.c_int),('charmaps',C.c_void_p),('generic',Generic),('bbox',Box),('units_per_EM',C.c_ushort)]+[
        (name,C.c_short) for name in ('ascender','descender','height','max_advance_width','max_advance_height',
                                    'underline_position','underline_thickness')]+[
        ('glyph',C.POINTER(Slot)),('size',C.c_void_p),('charmap',C.c_void_p)]


class FreeType:
    def __init__(self,path,pixels,hinting):
        if sys.platform!='linux' or C.sizeof(C.c_void_p)!=8 or C.sizeof(C.c_long)!=8:
            raise RuntimeError('参考绑定仅支持WSL/Linux LP64，不猜Windows结构布局')
        self.api=C.CDLL(find_library('freetype') or 'libfreetype.so.6')
        definitions={'FT_Init_FreeType':([C.POINTER(C.c_void_p)],C.c_int),
                     'FT_Library_Version':([C.c_void_p,C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(C.c_int)],None),
                     'FT_New_Face':([C.c_void_p,C.c_char_p,C.c_long,C.POINTER(C.POINTER(Face))],C.c_int),
                     'FT_Set_Pixel_Sizes':([C.POINTER(Face),C.c_uint,C.c_uint],C.c_int),
                     'FT_Get_Char_Index':([C.POINTER(Face),C.c_ulong],C.c_uint),
                     'FT_Load_Glyph':([C.POINTER(Face),C.c_uint,C.c_int32],C.c_int),
                     'FT_Render_Glyph':([C.POINTER(Slot),C.c_int],C.c_int),
                     'FT_Done_Face':([C.POINTER(Face)],C.c_int),'FT_Done_FreeType':([C.c_void_p],C.c_int)}
        for name,(arguments,result) in definitions.items():
            function=getattr(self.api,name);function.argtypes=arguments;function.restype=result
        self.library=C.c_void_p();self.face=C.POINTER(Face)()
        self.check(self.api.FT_Init_FreeType(C.byref(self.library)))
        self.check(self.api.FT_New_Face(self.library,str(path.resolve()).encode(),0,C.byref(self.face)))
        self.check(self.api.FT_Set_Pixel_Sizes(self.face,0,pixels))
        major,minor,patch=C.c_int(),C.c_int(),C.c_int()
        self.api.FT_Library_Version(self.library,C.byref(major),C.byref(minor),C.byref(patch))
        paths={line.split()[-1] for line in Path('/proc/self/maps').read_text().splitlines() if 'libfreetype.so' in line}
        if len(paths)!=1: raise RuntimeError('FreeType运行库路径不能唯一确认')
        library_path=Path(paths.pop())
        self.metadata=dict(renderer='FreeType direct FT_RENDER_MODE_MONO',hinting=hinting,
                           version=f'{major.value}.{minor.value}.{patch.value}',library=str(library_path),
                           library_sha256=hashlib.sha256(library_path.read_bytes()).hexdigest())
        # NO_BITMAP排除其它嵌入字形；原合同不执行字体指令或自动调整轮廓，
        # 必须明确NO_HINTING。另保留hinted模式，仅用于解释旧对照差异。
        self.flags=(1<<3)|(2<<16)|(0 if hinting else (1<<1)|(1<<15))
        self.metadata['load_flags']=self.flags

    @staticmethod
    def check(error):
        if error: raise RuntimeError(f'FreeType returned {error}')

    def render(self,scalar,glyph,width,height,left,baseline,advance):
        if self.api.FT_Get_Char_Index(self.face,scalar)!=glyph:
            raise AssertionError('独立FreeType映射与实际glyph ID不符')
        self.check(self.api.FT_Load_Glyph(self.face,glyph,self.flags))
        slot=self.face.contents.glyph
        self.check(self.api.FT_Render_Glyph(slot,2))
        view=slot.contents;bitmap=view.bitmap
        if view.advance.x!=advance*64 or bitmap.width>256 or bitmap.rows>256 or bitmap.pixel_mode!=1:
            raise AssertionError('FreeType实际指标/单色格式与合同不符')
        output=bytearray(width*height)
        for row in range(bitmap.rows):
            for col in range(bitmap.width):
                if not bitmap.buffer[row*bitmap.pitch+col//8]&(128>>(col%8)): continue
                x=view.bitmap_left-left+col;y=baseline-view.bitmap_top+row
                if x<0 or y<0 or x>=width or y>=height:
                    raise AssertionError('客体画布裁掉独立参考的真实墨迹')
                output[y*width+x]=255
        return bytes(output)

    def close(self):
        if self.face:self.api.FT_Done_Face(self.face)
        if self.library:self.api.FT_Done_FreeType(self.library)


def compare(font,dump,hinting):
    blob=dump.read_bytes()
    magic,version,face,count=struct.unpack_from('<4I',blob)
    if magic!=0x31464D53 or version!=1 or face not in (12,16) or len(blob)<80:
        raise ValueError('不是已验证的真实全映射转储')
    engine=FreeType(font,face,hinting);at=80;mismatches=[];actual_hash=hashlib.sha256();reference_hash=hashlib.sha256()
    try:
        info=struct.unpack_from('<16I',blob,16)
        if engine.face.contents.num_glyphs!=info[4] or engine.face.contents.units_per_EM!=info[6]:
            raise AssertionError('FreeType公共结构/字体元数据不符')
        for index in range(count):
            scalar,advance=struct.unpack_from('<2I',blob,at);head=struct.unpack_from('<16I',blob,at+8);at+=72
            width,height,baseline,left=head[6:10];left=left if left<0x80000000 else left-0x100000000
            words=(width+31)//32
            rows=struct.unpack_from('<'+str(height*words)+'I',blob,at) if words else ();at+=height*words*4
            actual=bytes(255 if rows[y*words+x//32]&(1<<(31-x%32)) else 0 for y in range(height) for x in range(width))
            expected=engine.render(scalar,head[2],width,height,left,baseline,advance)
            actual_hash.update(actual);reference_hash.update(expected)
            if actual!=expected:
                mismatches.append(dict(scalar=f'U+{scalar:04X}',glyph=head[2],different_pixels=sum(a!=b for a,b in zip(actual,expected))))
        if at!=len(blob): raise ValueError('记录数量/尾字节不符')
        return dict(status='ALL_NATIVE_GLYPH_PIXELS_MATCH' if not mismatches else 'NATIVE_PIXEL_MISMATCH',
                    face=face,mapped_checked=count,mismatches=mismatches,reference=engine.metadata,
                    font_sha256=hashlib.sha256(font.read_bytes()).hexdigest(),dump_sha256=hashlib.sha256(blob).hexdigest(),
                    actual_pixels_sha256=actual_hash.hexdigest(),reference_pixels_sha256=reference_hash.hexdigest())
    finally:
        engine.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font',type=Path,required=True);parser.add_argument('--dump',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True);parser.add_argument('--hinting',action='store_true')
    args=parser.parse_args();result=compare(args.font,args.dump,args.hinting)
    with args.out.open('x',encoding='utf-8') as output: output.write(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    print(result['status'],result['mapped_checked'],len(result['mismatches']))
    if result['mismatches']: sys.exit(1)


if __name__=='__main__':
    main()
