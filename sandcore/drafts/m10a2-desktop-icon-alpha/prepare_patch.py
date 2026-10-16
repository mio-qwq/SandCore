"""只准备三环straight ARGB补丁；原资源与冻结主树不写入。"""
from pathlib import Path
import hashlib,json,difflib
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
PINS={
 'user/scui/ui_paint.c':'e17e1f9d31e1f10cd485aca88c68009f5feed99ca6edf5ca70e704767b33cecb',
 'user/scui/ui_argb.c':'0e2f5b6a71abeda13cdeda2d71635511d57f32e724950891d58a4ac3f807ddd6',
 'user/scui/SCUIPRIVATE.H':'374c2c98c7da78cd7b8956764ec1b9143d1fec20b3361444634b2fd8658ca3a0'}
SLOW_HELPER='''/* 只供两端alpha都在1..254的慢路。文件/公开像素始终是straight ARGB，
 * 不能把透明目标的RGB当黑底后强制A255；否则桌面柔边会成为黑色实体。
 * 权重和<=65024，通道累积加舍入<2^24；原生32位整数足够，不用FP/宽除法。
 * 常见A0/A255、透明目标和不透明目标均在调用处处理，SCCC无需逐像素调用。 */
static ScU32 ui_argb_over_partial(ScU32 source,ScU32 target)
{
    ScU32 alpha=source>>24,target_alpha=target>>24;
    ScU32 source_weight=alpha*255u,target_weight=target_alpha*(255u-alpha);
    ScU32 denominator=source_weight+target_weight;
    ScU32 result=((denominator+127u)/255u)<<24;
    for(ScU32 shift=0;shift<24;shift+=8){
        ScU32 value=((source>>shift)&255u)*source_weight+((target>>shift)&255u)*target_weight;
        result|=((value+denominator/2u)/denominator)<<shift;
    }
    return result;
}
'''
def sha(body):return hashlib.sha256(body).hexdigest()
def one(text,old,new):
    if text.count(old)!=1:raise ValueError('补丁锚点变化：'+old[:80])
    return text.replace(old,new)
def candidates():
    before={};after={}
    for name,wanted in PINS.items():
        raw=(ROOT/name).read_bytes()
        if sha(raw)!=wanted:raise ValueError('冻结原像已变化：'+name)
        before[name]=raw;text=raw.decode('utf-8')
        if name.endswith('SCUIPRIVATE.H'):
            anchor='static void ui_border(ScUiPainter *,const ScUiRect *,int);'
            text=one(text,anchor,anchor+'\nstatic ScU32 ui_argb_over_partial(ScU32,ScU32);')
        elif name.endswith('ui_paint.c'):
            anchor='ScStatus ScUiPainterIcon(ScUiPainter *p,const ScUiRect *r,ScUiIcon *icon,ScU32 state)'
            text=one(text,anchor,SLOW_HELPER+'\n'+anchor)
            old='''        else if(alpha){ScU32 shift,result=0xff000000u,old=*target;
            for(shift=0;shift<24;shift+=8)result|=(((((color>>shift)&255u)*alpha+((old>>shift)&255u)*(255-alpha)+127)/255)<<shift);
            *target=result;
            }'''
            new='''        else if(alpha){ScU32 old=*target,target_alpha=old>>24;
            if(!target_alpha)*target=color;
            else if(target_alpha==255u){ScU32 shift,result=0xff000000u;
                for(shift=0;shift<24;shift+=8)result|=(((((color>>shift)&255u)*alpha+((old>>shift)&255u)*(255-alpha)+127)/255)<<shift);
                *target=result;
            }
            else *target=ui_argb_over_partial(color,old);
            }'''
            text=one(text,old,new)
        else:
            old='''            else if(alpha){ScU32 old=target[x],value=0xff000000u;
                for(ScU32 shift=0;shift<24;shift+=8)value|=(((((color>>shift)&255u)*alpha+((old>>shift)&255u)*(255u-alpha)+127u)/255u)<<shift);
                target[x]=value;
            }'''
            new='''            else if(alpha){ScU32 old=target[x],target_alpha=old>>24;
                if(!target_alpha)target[x]=color;
                else if(target_alpha==255u){ScU32 value=0xff000000u;
                    for(ScU32 shift=0;shift<24;shift+=8)value|=(((((color>>shift)&255u)*alpha+((old>>shift)&255u)*(255u-alpha)+127u)/255u)<<shift);
                    target[x]=value;
                }
                else target[x]=ui_argb_over_partial(color,old);
            }'''
            text=one(text,old,new)
        after[name]=text.encode('utf-8')
    if any((ROOT/n).read_bytes()!=b for n,b in before.items()):raise ValueError('补丁准备期间原像改变')
    return before,after
def main():
    before,after=candidates();patch=''
    for name in PINS:patch+=''.join(difflib.unified_diff(before[name].decode().splitlines(True),after[name].decode().splitlines(True),fromfile='a/sandcore/'+name,tofile='b/sandcore/'+name))
    (HERE/'straight-alpha.patch').write_text(patch,encoding='utf-8',newline='\n')
    (HERE/'pins.json').write_text(json.dumps(PINS,indent=2)+'\n',encoding='utf-8')
    (HERE/'result-pins.json').write_text(json.dumps({n:sha(b) for n,b in after.items()},indent=2)+'\n',encoding='utf-8')
    print('STRAIGHT_ALPHA_PATCH_PREPARED_NOT_APPLIED targets=3')
if __name__=='__main__':main()
