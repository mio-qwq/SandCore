#!/usr/bin/env python3
"""逐字节比较真实HMP原生工作区；仅排除已声明的动态任务栏。"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def pixels(record, taskbar):
    png = Path(record['path'])
    if hashlib.sha256(png.read_bytes()).hexdigest()!=record['sha256']:
        raise ValueError('登记截图摘要改变')
    ppm = png.with_suffix('.ppm')
    blob = ppm.read_bytes()
    header = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s',blob)
    if not header:
        raise ValueError('原始HMP PPM不完整')
    width,height = map(int,header.groups())
    body = blob[header.end():]
    if len(body)!=width*height*3 or [width,height]!=[record['width'],record['height']] or not 0<=taskbar<height:
        raise ValueError('原生尺寸/像素长度/任务栏高度不符')
    return width,height,body[:width*(height-taskbar)*3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before',type=Path,required=True)
    parser.add_argument('--after',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--taskbar-pixels',type=int,required=True)
    args = parser.parse_args()
    before = json.loads(args.before.read_text(encoding='utf-8'))
    after = json.loads(args.after.read_text(encoding='utf-8'))
    if before['status']!='COLLECTED' or after['status']!='COLLECTED' or len(before['disks'])!=len(after['disks']):
        raise ValueError('两批完整矩阵必须已收齐')
    report = dict(scope='ACTUAL_NATIVE_WORKAREA_EXCLUDING_DECLARED_TASKBAR_ONLY',status='COMPARING',
                  before=str(args.before.resolve()),after=str(args.after.resolve()),
                  excluded_bottom_pixels=args.taskbar_pixels,disks=[])
    failed = False
    for index,(old,new) in enumerate(zip(before['disks'],after['disks']),1):
        if any(old[key]!=new[key] for key in ('fixture_sources','workers','interval_ticks')):
            raise ValueError('夹具或负载参数不同')
        if [item['key'] for item in old['inputs']]!=[item['key'] for item in new['inputs']]:
            raise ValueError('真实输入序列不同')
        # 每次观察最后一张才代表确认后实际画面；尝试次数可能不同，
        # 不能按数组位置错误配对或把第一张旧数字当最终结果。
        def indexed(records):
            result = {}
            for item in records:
                stem = Path(item['path']).stem
                if stem=='input-initial':
                    result[0] = item
                else:
                    match = re.fullmatch(r'input-(\d+)-(\d+)',stem)
                    if match:
                        result[int(match[1])] = item
            return result
        old_images,new_images = indexed(old['screenshots']),indexed(new['screenshots'])
        expected = set(range(len(old['inputs'])+1))
        if set(old_images)!=expected or set(new_images)!=expected:
            raise ValueError('初始及每次真实输入后的完整画面缺失')
        disk = dict(index=index,samples=[])
        for sample in sorted(expected):
            width,height,old_pixels = pixels(old_images[sample],args.taskbar_pixels)
            new_width,new_height,new_pixels = pixels(new_images[sample],args.taskbar_pixels)
            if [width,height]!=[new_width,new_height]:
                raise ValueError('前后原生尺寸不同')
            same = old_pixels==new_pixels
            entry = dict(sample=sample,width=width,height=height,compared_height=height-args.taskbar_pixels,
                         before_sha256=hashlib.sha256(old_pixels).hexdigest(),
                         after_sha256=hashlib.sha256(new_pixels).hexdigest(),exact_bytes_equal=same)
            if not same:
                failed = True
                different = [at for at in range(0,len(old_pixels),3) if old_pixels[at:at+3]!=new_pixels[at:at+3]]
                entry['different_pixels'] = len(different)
                entry['first_differences'] = [dict(x=(at//3)%width,y=(at//3)//width,
                       before=old_pixels[at:at+3].hex(),after=new_pixels[at:at+3].hex()) for at in different[:16]]
            disk['samples'].append(entry)
        report['disks'].append(disk)
    report['status'] = 'ACTUAL_WORKAREA_BYTES_EQUAL' if not failed else 'PIXEL_DIFFERENCES_FOUND'
    # 不覆盖历史对照或原截图，结果不修改任何图像像素。
    with args.out.open('x',encoding='utf-8') as output:
        output.write(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(report['status'],flush=True)
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
