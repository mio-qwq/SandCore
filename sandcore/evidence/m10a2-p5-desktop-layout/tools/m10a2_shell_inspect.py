#!/usr/bin/env python3
"""只读暂停快照：按本批真实i386头布局观察host的私有SCUI状态。"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
FIELDS = {
    'record_size':'sizeof(task_record_t)', 'record_generation':'O(task_record_t,generation)',
    'record_pd':'O(task_record_t,task)+O(task_t,pd)', 'record_state':'O(task_record_t,task)+O(task_t,state)',
    'pool_stride':'O(objpool_t,stride)', 'desk_size':'sizeof(desk)', 'desk_ui':'O(__typeof__(desk),ui)',
    'context_size':'sizeof(ScUiContext)', 'context_roots':'O(ScUiContext,roots)',
    'context_stopped':'O(ScUiContext,stopped)', 'context_dispatching':'O(ScUiContext,dispatching)',
    'context_walking':'O(ScUiContext,walking)', 'context_batch':'O(ScUiContext,batch)',
    'context_messages':'O(ScUiContext,received_messages)', 'context_paints':'O(ScUiContext,paint_count)',
    'context_quantum':'O(ScUiContext,description)+O(ScUiContextDesc,scheduling_quantum)',
    'node_size':'sizeof(UiNode)', 'node_next':'O(UiNode,root_next)', 'node_id':'O(UiNode,id)',
    'node_kind':'O(UiNode,kind)', 'node_state':'O(UiNode,state)', 'node_private':'O(UiNode,private_flags)',
    'node_window':'O(UiNode,window)', 'node_rectangle':'O(UiNode,rectangle)',
    'root_size':'sizeof(UiRoot)', 'root_window':'O(UiRoot,window)',
    'root_information_state':'O(UiRoot,information)+O(ScWindowInfo,state)',
    'root_information_geometry':'O(UiRoot,information)+O(ScWindowInfo,geometry_epoch)',
    'root_visible':'O(UiRoot,visible)', 'root_pending':'O(UiRoot,paint_pending)',
    'root_failures':'O(UiRoot,paint_failures)', 'root_geometry':'O(UiRoot,geometry)',
    'root_width':'O(UiRoot,width)', 'root_height':'O(UiRoot,height)', 'root_retry':'O(UiRoot,retry)',
}
for name in ('items','count','grid_w','grid_h','layout_mode','store','store_revision','save_pending',
             'layout_unsaved','layout_serial','store_serial','icons_pending','icon_errors',
             'refresh_pending','gesture','scroll_x','scroll_y','active','visible','store_path'):
    FIELDS['desk_'+name]='O(Desktop,'+name+')'
FIELDS['desk_scale']='O(Desktop,metrics)+O(ScUiMetrics,scale_percent)'
FIELDS['item_size']='sizeof(DeskItem)'
for name in ('identity','label','x','y','selected','removed','icon_failed'):
    FIELDS['item_'+name]='O(DeskItem,'+name+')'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build_layout(stage, out):
    out.mkdir(parents=True, exist_ok=False)
    source = out / 'layout.c'
    source.write_text('#include "kernel/task_store.h"\n#include "user/scui/SCUIPRIVATE.H"\n'
        '#include "user/scshell/desktop/DESKTOP.H"\n#define O(t,f) __builtin_offsetof(t,f)\n'
        'const u32 layout[] __attribute__((section(".p5layout"),used))={\n'
        + ',\n'.join(FIELDS.values()) + '\n};\n', encoding='utf-8')
    command = ['gcc','-m32','-ffreestanding','-fno-builtin','-fno-pie','-O0',
        '-I'+str(stage),'-I'+str(stage/'user'),'-I'+str(stage/'user/stdlib'),
        '-I'+str(stage/'user/sclib'),'-I'+str(stage/'user/runtime'),
        '-c',str(source),'-o',str(out/'layout.o')]
    result = subprocess.run(command, capture_output=True, text=True)
    (out/'build.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(result.stderr[-2000:])
    subprocess.run(['objcopy','-O','binary','--only-section=.p5layout',str(out/'layout.o'),str(out/'layout.bin')], check=True)
    data = (out/'layout.bin').read_bytes()
    if len(data)!=len(FIELDS)*4:
        raise ValueError('布局常量段长度不同')
    value = dict(schema=1, method='ACTUAL_I386_GCC_OFFSETOF_NO_GUEST_EXECUTION',
        values=dict(zip(FIELDS,struct.unpack('<'+'I'*len(FIELDS),data))),
        headers={name:sha(stage/name) for name in ('kernel/task_store.h','kernel/task.h','kernel/objpool.h',
            'user/scui/SCUIPRIVATE.H','user/scshell/desktop/DESKTOP.H')},
        command=command, source_sha256=sha(source))
    (out/'layout.json').write_text(json.dumps(value,indent=2)+'\n', encoding='utf-8')
    print('READ_ONLY_LAYOUT',value['values']['desk_size'],value['values']['context_size'],value['values']['root_size'])


def inspect(vm, symbols, layout, desktop_map, pid, generation):
    offsets = layout['values']
    observed = dict(scope='READ_ONLY_PAUSED_PREPARED_HOST', pid=pid,generation=generation,
        desktop_map_sha256=sha(desktop_map),layout=layout,guest_memory_writes=False,
        started=time.monotonic())
    physical_pages={}

    def physical(address,count=1):
        if time.monotonic()-observed['started']>2:
            raise TimeoutError('只读暂停预算到达，立即恢复')
        values=[]
        for i in range(count):
            current=address+i*4;page=current&~4095
            if page not in physical_pages:
                answer=vm.hmp('xp /1024wx 0x%08x' % page)
                words=re.findall(r'0x([0-9a-fA-F]{8})',answer)
                if len(words)!=1024:raise ValueError('只读物理页快照长度不同')
                physical_pages[page]=[int(word,16) for word in words]
            values.append(physical_pages[page][(current&4095)//4])
        return values

    def pool_object(pool, key):
        a=physical(pool+(key>>23)*4)[0]
        b=physical(a+((key>>14)&511)*4)[0] if a else 0
        block=physical(b+((key>>5)&511)*4)[0] if b else 0
        stride=physical(pool+offsets['pool_stride'])[0]
        if not block or not physical(block)[0]&(1<<(key&31)):
            raise ValueError('暂停时PID记录已经失效')
        return block+64+(key&31)*stride,stride

    vm.qmp('stop')
    try:
        # records在本批core.sym中唯一；TTF专用scoped表刻意不枚举其它对象。
        record,stride=pool_object(symbols['records'],pid)
        if stride!=((offsets['record_size']+15)&~15):
            raise ValueError('实际task_record步长与本批头不同')
        if physical(record+offsets['record_generation'])[0]!=generation:
            raise ValueError('暂停前PID已经复用')
        pd=physical(record+offsets['record_pd'])[0]
        observed.update(record=record,pd=pd,task_state=physical(record+offsets['record_state'])[0])
        page_cache={}

        def virtual(address,count=1):
            result=[]
            for i in range(count):
                value=0
                # 现有SCX的BSS可紧跟变长metadata，绝不能按地址向下取整
                # 读u32。逐字节处理也覆盖跨虚拟页且物理页不连续的字段。
                for byte in range(4):
                    current=address+i*4+byte;page=current&~4095
                    if page not in page_cache:
                        pde=physical(pd+(current>>22)*4)[0]
                        if pde&5!=5:raise ValueError('目标PDE不是用户映射')
                        pte=physical((pde&~4095)+((current>>12)&1023)*4)[0]
                        if pte&5!=5:raise ValueError('目标PTE不是用户映射')
                        page_cache[page]=pte&~4095
                    pa=page_cache[page]+(current&4095)
                    word=physical(pa&~3)[0]
                    value|=((word>>((pa&3)*8))&255)<<(byte*8)
                result.append(value)
            return result

        matches=re.findall(r'^OBJECT ([0-9A-Fa-f]+) desk (\d+)$',desktop_map.read_text(encoding='utf-8'),re.M)
        if len(matches)!=1 or int(matches[0][1])!=offsets['desk_size']:
            raise ValueError('实际原生Desk对象大小与i386头布局不一致')
        desk=int(matches[0][0],16)
        context=virtual(desk+offsets['desk_ui'])[0]
        observed.update(desk=desk,context=context)
        observed['context_state']={name:virtual(context+offsets['context_'+name])[0]
            for name in ('stopped','dispatching','walking','batch','quantum')}
        observed['context_state']['messages']=virtual(context+offsets['context_messages'],2)
        observed['context_state']['paints']=virtual(context+offsets['context_paints'],2)
        node=virtual(context+offsets['context_roots'])[0]
        roots=[]
        for _ in range(8):
            if not node:break
            root=virtual(node+offsets['node_window'])[0]
            item=dict(node=node,root_pointer=root,
                id=virtual(node+offsets['node_id'])[0],kind=virtual(node+offsets['node_kind'])[0],
                state=virtual(node+offsets['node_state'])[0],private=virtual(node+offsets['node_private'])[0],
                rectangle=virtual(node+offsets['node_rectangle'],4))
            if root:
                item['window_state']={name:virtual(root+offsets['root_'+name])[0] for name in
                    ('window','information_state','information_geometry','visible','pending','failures','geometry','width','height')}
                item['window_state']['retry']=virtual(root+offsets['root_retry'],2)
            roots.append(item);node=virtual(node+offsets['node_next'])[0]
        observed['roots']=roots
        if 'desk_items' in offsets:
            def text(address,maximum=68):
                data=b''.join(struct.pack('<I',word) for word in virtual(address,(maximum+3)//4))
                return data[:maximum].split(b'\0',1)[0].decode('utf-8','strict')

            state={name:virtual(desk+offsets['desk_'+name])[0] for name in
                ('items','count','grid_w','grid_h','layout_mode','store','store_revision','save_pending',
                 'layout_unsaved','layout_serial','store_serial','icons_pending','icon_errors',
                 'refresh_pending','gesture','scroll_x','scroll_y','active','visible','scale')}
            state['store_path']=text(desk+offsets['desk_store_path'])
            state['rows']=[]
            for i in range(min(state['count'],32)):
                item=state['items']+i*offsets['item_size']
                row={name:virtual(item+offsets['item_'+name])[0] for name in
                    ('x','y','selected','removed','icon_failed')}
                row['identity']=text(virtual(item+offsets['item_identity'])[0])
                row['label']=text(virtual(item+offsets['item_label'])[0],128)
                state['rows'].append(row)
            observed['desktop_model']=state
    except Exception as error:
        observed['error']=repr(error)
    finally:
        vm.qmp('cont')
        observed['paused_wall_seconds']=time.monotonic()-observed['started']
    return observed


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    build_layout(args.stage.resolve(),args.out.resolve())
