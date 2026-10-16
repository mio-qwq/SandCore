"""只读现有截图与SCB，记录根因像素；不写、重绘或转换任何图像资产。"""
from pathlib import Path
import collections,hashlib,json,mmap,struct,sys
from PIL import Image
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from mkfs_m9 import read_image
from mkfs_m10 import release_views
def sha(body):return hashlib.sha256(body).hexdigest()
def images_from(path):
    result={}
    with path.open('rb') as stream:
        with mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ) as mapped:
            rows=read_image(mapped,False)
            try:
                for name in ('FILES','NOTE','STUDIO','CALC','SHELL','LENS','SOUND','PALETTE'):
                    key='SYS/ICONS/AURORA/'+name+'.SCB';result[key]=bytes(rows[key].payload)
            finally:release_views(rows)
    return result
def main():
    base=images_from(ROOT/'build/m10a1/sanddata.img');gui=images_from(ROOT/'build/m10a2-p6-gui-run-01/sanddata.img');stats={}
    for name,body in gui.items():
        if body!=base[name]:raise ValueError('原盘与GUI图标原件不等：'+name)
        w,h,kind,flags,n,trailer=struct.unpack_from('<6I',body,8)
        if body[:8]!=b'SCB2MIO\0' or (w,h,kind,flags,n,trailer)!=(128,128,2,0,65536,0x004f494d) or len(body)!=65568:raise ValueError('实际SCB2合同不同')
        pixels=struct.unpack_from('<16384I',body,32);alpha=collections.Counter(p>>24 for p in pixels)
        stats[name]=dict(bytes=len(body),sha256=sha(body),width=w,height=h,format='STRAIGHT_ARGB_SCB2_FLAGS_ZERO',same_bytes_as_original_m10a1=True,
            alpha_zero=alpha[0],alpha_partial=sum(v for k,v in alpha.items() if 0<k<255),alpha_opaque=alpha[255],distinct_alpha=len(alpha),opaque_white=sum(p==0xffffffff for p in pixels),opaque_black=sum(p==0xff000000 for p in pixels))
    shot=ROOT/'build/m10a2-p6-gui-run-01/p6-native-desktop-initial.png';screen=Image.open(shot).convert('RGB').load();pixel=struct.unpack_from('<16384I',gui['SYS/ICONS/AURORA/FILES.SCB'],32)
    opaque=exact=partial=black_exact=0;partial_differences=[]
    for y in range(72):
        for x in range(72):
            value=pixel[(y*128//72)*128+x*128//72];alpha=value>>24;observed=screen[96+x,48+y]
            rgb=tuple((value>>k)&255 for k in (16,8,0))
            if alpha==255:opaque+=1;exact+=observed==rgb
            elif alpha and not(x<13 and y>=59):
                partial+=1;wrong=tuple((v*alpha+127)//255 for v in rgb);black_exact+=observed==wrong
                if observed!=wrong:partial_differences.append(dict(screen=[96+x,48+y],source_argb='%08x'%value,expected_wrong_black=wrong,actual=observed))
    if opaque!=1640 or exact!=opaque:raise ValueError('实际截图不等于已确认nearest几何')
    samples=[]
    for x,y in ((105,66),(107,67),(125,66)):
        sx=(x-96)*128//72;sy=(y-48)*128//72;value=pixel[sy*128+sx];alpha=value>>24;rgb=tuple((value>>k)&255 for k in (16,8,0))
        samples.append(dict(screen=[x,y],source=[sx,sy],source_argb='%08x'%value,actual_rgb=screen[x,y],wrong_opaque_black_composite=[(v*alpha+127)//255 for v in rgb],correct_clear_destination_argb='%08x'%value))
    note=struct.unpack_from('<16384I',gui['SYS/ICONS/AURORA/NOTE.SCB'],32);white=white_exact=0
    for y in range(72):
        for x in range(72):
            value=note[(y*128//72)*128+x*128//72]
            if value==0xffffffff:white+=1;white_exact+=screen[288+x,48+y]==(255,255,255)
    sources=('user/scui/ui_paint.c','user/scui/ui_argb.c','user/scui/ui_data.c','user/scui/SCUIPRIVATE.H','user/scui/ui_shell_paint.inc','user/scui/ui_menu.c','user/scshell/shell_shortcut.c','user/scshell/desktop/desktop_view.inc','user/SCUIARGB.H','docs/IMAGE.md','docs/GFX.md')
    native_path=ROOT/'build/m10a2-p6-guest-generation-run-03/p6-generation.json';native=json.loads(native_path.read_text(encoding='utf-8'));ui=native['native_libraries']['SCUI.SCL'];case=next(x for x in native['cases'] if x['name']==ui['object_compile_case'])
    source_sha={n:sha((ROOT/n).read_bytes()) for n in sources}
    for n in ('user/scui/ui_paint.c','user/scui/ui_argb.c','user/scui/ui_data.c','user/scui/SCUIPRIVATE.H','user/scui/ui_shell_paint.inc'):
        if source_sha[n]!=case['source_input_sha256'][n]:raise ValueError('当前源不是实际原生UI生成源：'+n)
    pictures={n:dict(bytes=(ROOT/n).stat().st_size,sha256=sha((ROOT/n).read_bytes())) for n in ('build/m10a2-p6-gui-run-01/p6-native-desktop-initial.png','build/m10a2-p6-gui-run-01/p6-native-system-menu-settled.png')}
    value=dict(schema=1,scope='READ_ONLY_EXISTING_SCREENSHOT_AND_ORIGINAL_SCB_PIXEL_FORENSICS',resources=stats,screenshots=pictures,
        files_nearest_geometry=dict(x=96,y=48,width=72,height=72,opaque_samples=opaque,opaque_rgb_exact=exact,partial_samples_excluding_shortcut_badge=partial,partial_exact_wrong_black_composite=black_exact,remaining_samples=partial_differences),
        note_opaque_white_samples=white,note_opaque_white_exact=white_exact,examples=samples,source_sha256=source_sha,native_ui=dict(path=ui['path'],bytes=ui['bytes'],sha256=ui['sha256'],object_compile_case=ui['object_compile_case']),asset_edited=False,image_generated=False,vm_started=False,fix_runtime_verified=False)
    (HERE/'PIXEL-EVIDENCE.json').write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(opaque_exact=exact,opaque=opaque,partial=partial,partial_black_exact=black_exact,note_white=white,note_white_exact=white_exact),indent=2))
if __name__=='__main__':main()
