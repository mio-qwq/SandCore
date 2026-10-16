"""真实PS/2桌面手势；只读模型观察与实际保存的catalog交叉核验。"""
import hashlib
import json
import struct
import time
import zlib

from m10a2_shell_inspect import inspect
from scui_input import ScuiInput


def catalog(path):
    data=path.read_bytes()
    if len(data)<64:raise ValueError('catalog头截断')
    header=struct.unpack_from('<16I',data)
    if header[:3]!=(0x54414353,1,64) or header[3]!=len(data)-64 or zlib.crc32(data[64:])!=header[4]:
        raise ValueError('实际catalog版本/尺寸/CRC错误')
    cursor=64
    for count,minimum,index in ((header[7],112,25),(header[8],48,2)):
        for _ in range(count):
            if len(data)-cursor<minimum:raise ValueError('catalog记录截断')
            row=struct.unpack_from('<'+'I'*(minimum//4),data,cursor)
            size=row[index]
            if row[:2]!=(minimum,1) or size<minimum or size>len(data)-cursor or size&3:
                raise ValueError('catalog记录长度错误')
            cursor+=size
    rows=[]
    for _ in range(header[9]):
        if len(data)-cursor<48:raise ValueError('布局记录截断')
        row=struct.unpack_from('<12I',data,cursor)
        size,offset,count=row[2],row[10],row[11]
        if row[:2]!=(48,1) or size<48 or size>len(data)-cursor or offset!=48 or not count or count>size-offset:
            raise ValueError('布局记录范围错误')
        text=data[cursor+offset:cursor+offset+count]
        if text[-1] or b'\0' in text[:-1]:raise ValueError('布局身份错误')
        rows.append(dict(identity=text[:-1].decode('utf-8'),x=row[5],y=row[6],width=row[7],height=row[8]))
        cursor+=size
    if cursor!=len(data):raise ValueError('catalog未解释尾部')
    return dict(sha256=hashlib.sha256(data).hexdigest(),bytes=len(data),crc=True,epoch=header[5],
        layout_mode=header[10],grid_width=header[11],grid_height=header[12],revision=header[13],rows=rows)


def run(vm,guest,symbols,harness,report,layout,desktop_map,identity,out,expected=None):
    result=dict(status='RUNNING',guest_memory_writes=False,physical_gestures=True,observations=[])
    report['desktop_layout_flow']=result
    inputs=ScuiInput(vm,guest,symbols,report['physical_input'].append)

    def observe():
        snapshot=inspect(vm,symbols,layout,desktop_map,identity['pid'],identity['generation'])
        if 'error' in snapshot or 'desktop_model' not in snapshot:
            raise ValueError('只读模型观察无效：'+str(snapshot.get('error')))
        result['last_snapshot']=snapshot
        state=snapshot['desktop_model']
        result['observations'].append({name:state[name] for name in
            ('count','icons_pending','icon_errors','store','save_pending','layout_unsaved','layout_serial','store_serial','gesture')})
        return state

    def ready(predicate=lambda state:True):
        deadline=time.monotonic()+30
        while True:
            state=observe()
            stable=result['last_snapshot']
            if (state['count']>=3 and state['count']<=32 and not state['refresh_pending']
                    and not state['icons_pending'] and not state['store'] and not state['save_pending']
                    and not state['layout_unsaved'] and not state['gesture']
                    and all(row['removed'] or not row['shortcut'] or row['icon'] or row['icon_failed'] for row in state['rows'])
                    and not stable['context_state']['dispatching'] and not stable['context_state']['walking']
                    and all(not row.get('window_state',{}).get('pending',0) for row in stable['roots'])
                    and predicate(state)):
                return state
            if time.monotonic()>=deadline:raise TimeoutError('桌面实际资源/持久保存未在30秒内就绪')
            guest.stop.wait(.4)

    # 当前selected SID刚显示时，壁纸是另一条task0异步链；不能以R3的
    # “已绘首帧”冒称原始壁纸完成。沿已有真实背景/帧观察等其完成。
    harness.warm_desktop()
    before=ready();result['before']=before
    if before['icon_errors']:raise ValueError('正式桌面图标加载失败：'+str(before['icon_errors']))
    harness.screen('p5-native-desktop-resources-ready')
    if expected:
        saved=expected['desktop_layout_flow']['saved_catalog']
        by_id={row['identity']:row for row in before['rows']}
        for row in saved['rows']:
            actual=by_id.get(row['identity'])
            if not actual or (actual['x'],actual['y'])!=(row['x'],row['y']):
                raise ValueError('全新QEMU启动未恢复实际保存的布局：'+row['identity'])
        actual=out/'cold-restored-SHELL.CAT';guest.client.get(before['store_path'],actual)
        persisted=catalog(actual)
        if persisted['sha256']!=saved['sha256']:raise ValueError('冷启动改变了原保存布局字节')
        result.update(status='NATIVE_DESKTOP_COLD_LAYOUT_PASS',saved_catalog=persisted,
            restored_items=len(saved['rows']),cold_start_new_qemu=True)
        harness.screen('p5-native-desktop-cold-restored')
        return result

    scale=before['scale']
    def px(value):return (value*scale+50)//100
    def point(row):return (px(row['x']+before['grid_w']//2)-before['scroll_x'],
                           px(row['y']+20)-before['scroll_y'])
    def drag(start,end):
        inputs.move(*start);inputs.button('left',True);inputs.move(*end);inputs.button('left',False)

    first=before['rows'][0];start=point(first)
    drag(start,(start[0]+px(before['grid_w']),start[1]+px(before['grid_h']*2)))
    moved=ready(lambda state:any(row['identity']==first['identity'] and (row['x'],row['y'])!=(first['x'],first['y'])
        for row in state['rows']))
    result['after_single_drag']=moved
    harness.screen('p5-native-desktop-icon-moved')
    # 框选相邻两个实际模型矩形，空白起点避免把按下误作图标拖动。
    targets=moved['rows'][1:3]
    left=min(px(row['x']) for row in targets)-4
    top=min(px(row['y']) for row in targets)-4
    right=max(px(row['x']+moved['grid_w']) for row in targets)+4
    bottom=max(px(row['y']+moved['grid_h']) for row in targets)+4
    drag((left,top),(right,bottom))
    selected=ready(lambda state:all(any(row['identity']==target['identity'] and row['selected']
        for row in state['rows']) for target in targets))
    if next(row for row in selected['rows'] if row['identity']==first['identity'])['selected']:
        raise ValueError('框选把已移到框外的图标选中')
    result['after_box_selection']=selected
    start=point(targets[0]);drag(start,(start[0]+px(moved['grid_w']),start[1]+px(moved['grid_h']*3)))
    grouped=ready(lambda state:all(any(row['identity']==target['identity']
        and (row['x'],row['y'])!=(target['x'],target['y']) for row in state['rows']) for target in targets))
    result['after_group_drag']=grouped
    actual=out/'dragged-SHELL.CAT';guest.client.get(grouped['store_path'],actual)
    saved=catalog(actual);by_id={row['identity']:row for row in saved['rows']}
    for row in grouped['rows']:
        stored=by_id.get(row['identity'])
        if not stored or (row['x'],row['y'])!=(stored['x'],stored['y']):
            raise ValueError('真实已提交catalog与拖动模型不同：'+row['identity'])
    result.update(status='NATIVE_DESKTOP_DRAG_BOX_PERSIST_PASS',saved_catalog=saved,
        real_dragged_items=[first['identity']]+[row['identity'] for row in targets])
    harness.screen('p5-native-desktop-group-moved-and-saved')
    return result
