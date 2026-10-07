#!/usr/bin/env python3
"""用户签名的真实启动/扩展矩阵；原盘只读，逐例独立Windows QEMU。"""
import argparse
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import time
import traceback
import zlib

from core_signature import verify, verify_signature_only
from mkfs_m10 import check_main, digest_file, digest_payload, mapped, parents, release_views, write_image
from mkfs_m9 import Record, fold, read_image
from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm
from verify_m10_lifecycle import run_case
from serial_protocol import Decoder
from m10_guest_boot import wait_first_desktop
from m10_verification_report import checkpoint

ROOT = Path(__file__).resolve().parents[1]
PROFILE = ('positive', 'without-failed-init', 'rejections', 'duplicates', 'recovery', 'both-main-bad')


def artifacts(receipt, openssl):
    report = json.loads((receipt/'receipt.json').read_text(encoding='utf-8'))
    if report['status'] != 'SIX_USER_SIGNATURES_HOST_VERIFIED_RUNTIME_PENDING':
        raise ValueError('需要已完成独立验签的公开接收记录')
    key = (receipt/'public-key.bin').read_bytes()
    if hashlib.sha256(key).hexdigest() != report['public_key_sha256']:
        raise ValueError('接收记录公钥改变')
    blobs = {}
    for record in report['records']:
        # 只从公开接收目录约定子目录读取；JSON路径不作为任意文件指令。
        name = record['message'].removesuffix('.msg')
        folder = 'rejection-fixtures' if record.get('format_rejected') else 'signed'
        blob = (receipt/folder/name).read_bytes()
        if digest_payload(blob) != record['output_sha256']:
            raise ValueError('已签产物摘要改变：'+name)
        verify_signature_only(key, blob[:-64], blob[-64:], openssl)
        if folder == 'signed':
            verify(key, blob[:-64], blob[-64:], openssl)
        blobs[name] = blob
    expected = {'Z10-INIT.SKM','A20-SERVICE.SKM','B30-FAIL.SKM','WALL100.SKM','BAD-ABI.SKM','BAD-RELOC.SKM'}
    if set(blobs) != expected:
        raise ValueError('六份用户签署结果不齐')
    return key, blobs


def changed_signed(blob, field=None, body=False):
    altered = bytearray(blob)
    if field is not None:
        value = struct.unpack_from('<I', altered, field)[0]
        struct.pack_into('<I', altered, field, value+1)
    if body:
        altered[128] ^= 1
        altered[72:104] = hashlib.sha256(altered[128:-64]).digest()
    struct.pack_into('<I', altered, 124, zlib.crc32(altered[:124]))
    return bytes(altered)


def specification(profile, signed, main, legacy):
    files = {'SYS/CORE/A20-SERVICE.SKM': signed['A20-SERVICE.SKM'],
             'SYS/CORE/WALL100.SKM': signed['WALL100.SKM']}
    rejected, order, loaded, failed = {}, [20], 2, 0
    primary, recovery, boot_path = main, main, 'SYS/CORE/CORE.SKM'
    if profile in ('positive','without-failed-init','recovery'):
        files['SYS/CORE/Z10-INIT.SKM'] = signed['Z10-INIT.SKM']
        order, loaded = [10,20], 3
        if profile != 'without-failed-init':
            files['SYS/CORE/B30-FAIL.SKM'] = signed['B30-FAIL.SKM']
            order.append(30);failed = 1
            rejected['SYS/CORE/B30-FAIL.SKM'] = ('INIT_REJECT',7)
    elif profile == 'duplicates':
        files['SYS/CORE/Z10-INIT.SKM'] = signed['Z10-INIT.SKM']
        files['SYS/CORE/COPY10.SKM'] = signed['Z10-INIT.SKM']
        for path in ('SYS/CORE/Z10-INIT.SKM','SYS/CORE/COPY10.SKM'):
            rejected[path] = ('DUPLICATE_NUMBER',-10)
        failed = 2
    elif profile == 'rejections':
        for name in ('BAD-ABI.SKM','BAD-RELOC.SKM'):
            files['SYS/CORE/'+name] = signed[name]
            rejected['SYS/CORE/'+name] = ('REJECT',-1)
        original = signed['Z10-INIT.SKM']
        tampered_signature = bytearray(original);tampered_signature[-1] ^= 1
        for name, blob, reason in (
                ('UNSIGNED.SKM',original[:-64],('REJECT',-1)),
                ('TRUNCATED.SKM',original[:-1],('REJECT',-1)),
                ('LEGACY.SKM',legacy,('REJECT',-1)),
                ('BAD-SIGNATURE.SKM',bytes(tampered_signature),('BAD_SIGNATURE',-8)),
                ('TAMPER-NUMBER.SKM',changed_signed(original,24),('BAD_SIGNATURE',-8)),
                ('TAMPER-ENTRY.SKM',changed_signed(original,40),('BAD_SIGNATURE',-8)),
                ('TAMPER-PAYLOAD.SKM',changed_signed(original,body=True),('BAD_SIGNATURE',-8)),
                ('MAIN-AS-EXT.SKM',main,('REJECT',-1))):
            files['SYS/CORE/'+name] = blob;rejected['SYS/CORE/'+name] = reason
        # 有效签名放在非扫描位置，也不应执行编号10。恢复目录的主核
        # 独立存在；这个附加EXT只证明扫描不会越目录或按SCX后缀执行。
        files['SYS/CORE/IGNORED.SCX'] = original
        files['SYS/CORE/SUB/IGNORED.SKM'] = original
        files['SYS/RECOVERY/IGNORED.SKM'] = original
        failed = len(rejected)
    if profile in ('recovery','both-main-bad'):
        primary = bytearray(main);primary[128] ^= 1;primary = bytes(primary)
        boot_path = 'SYS/RECOVERY/CORE.SKM'
        if profile == 'both-main-bad':
            recovery = primary
    return dict(files=files,rejected=rejected,order=order,loaded=loaded,failed=failed,
                primary=primary,recovery=recovery,boot_path=boot_path)


def prepare(source, destination, spec):
    original_sha = digest_file(source)
    with ExitStack() as stack:
        disk = mapped(stack,source)
        records = read_image(disk,False)
        stack.callback(lambda: release_views(records))
        original = {key:(digest_payload(record.payload),record.uid,record.gid,record.mode,record.generation)
                    for key,record in records.items()}
        root = struct.unpack_from('<iiI',disk,32)
        commit,counter = struct.unpack_from('<II',disk,52)
        counter = max(counter,max(record.generation for record in records.values()))
        if commit == 0xffffffff:
            raise ValueError('目录代数耗尽')
        def generation():
            nonlocal counter
            if counter == 0xffffffff:
                raise ValueError('对象代数耗尽')
            counter += 1
            return counter
        # 仅验收副本控制开机文件集合；删除/替换列表入记录。历史源盘
        # 及发布构建树保持只读，不把这些坏文件交给正式发布入口。
        changed = {'SYS/CORE/CORE.SKM','SYS/RECOVERY/CORE.SKM'}
        for key in tuple(records):
            if key.startswith('SYS/CORE/') and key.count('/')==2 and key.endswith('.SKM') and key!='SYS/CORE/CORE.SKM':
                changed.add(key);del records[key]
            elif key in ('TMP/M10ORDER','TMP/M10SERVICE','TMP/M10FAILED','TMP/M10MODULE','TMP/M10SNAP'):
                changed.add(key);del records[key]
        additions = dict(spec['files'], **{'SYS/CORE/CORE.SKM':spec['primary'],
                                         'SYS/RECOVERY/CORE.SKM':spec['recovery']})
        for name, blob in additions.items():
            key = fold(name);changed.add(key)
            records[key] = Record(name,blob,-1,-1,23,generation())
        parents(records,generation)
        directory,count,used = write_image(destination,records,256,root,commit+1,counter)
        with ExitStack() as check_stack:
            output = mapped(check_stack,destination)
            checked = read_image(output,False)
            check_stack.callback(lambda: release_views(checked))
            for key,record in records.items():
                actual = checked[key]
                if (digest_payload(actual.payload),actual.uid,actual.gid,actual.mode,actual.generation) != (
                        digest_payload(record.payload),record.uid,record.gid,record.mode,record.generation):
                    raise AssertionError('测试盘回读不一致：'+key)
            preserved = 0
            for key,before in original.items():
                if key in changed:
                    continue
                actual = checked[key]
                if (digest_payload(actual.payload),actual.uid,actual.gid,actual.mode,actual.generation) != before:
                    raise AssertionError('非测试对象改变：'+key)
                preserved += 1
        unchanged = digest_file(source)==original_sha
        if not unchanged:
            raise AssertionError('源盘外部变化')
        return dict(source=str(source.resolve()),source_sha256=original_sha,source_unchanged=True,
                    destination_sha256=digest_file(destination),controlled_paths=sorted(changed),
                    preserved_other_objects=preserved,directory_sectors=directory,entries=count,data_end=used)


def symbol_table(path):
    result = {}
    for line in path.read_text(encoding='ascii').splitlines():
        fields = line.split()
        if len(fields)==3:
            result.setdefault(fields[2],[]).append(int(fields[0],16))
    required = ('residents','allocations','callbacks','snapshot','irq_heads','service_head','scene_owner')
    if any(len(result.get(name,[])) != 1 for name in required):
        raise ValueError('当前ELF符号缺失/重复，不能推测内部地址')
    return {name:values[0] for name,values in result.items() if len(values)==1}


def memory_read(guest,address,bytes_count):
    data = bytearray()
    while len(data)<bytes_count:
        amount = min(256,bytes_count-len(data))
        cursor = len(guest.debug)
        guest.vm.pipes['debug'].write(f'mem {address+len(data):08x} {amount:x}\n'.encode('ascii'))
        reply = guest.wait(rb'DATA ([0-9a-f]{'+str(amount*2).encode()+rb'})\r\n',cursor,debug=True)
        data.extend(bytes.fromhex(reply[1].decode()))
    return bytes(data)


def pool_snapshot(guest,address):
    words = struct.unpack('<6I',memory_read(guest,address+1088,24))
    result = dict(zip(('stride','flags','objects','pages','peak_pages','high_water'),words))
    # 本矩阵仅3个常驻/1辅助/2回调，故都落首叶；动态池数量合同由
    # 独立资源矩阵测试。这里真实读used位和活正文，证明失败者不存在。
    if result['high_water']>32:
        raise AssertionError('本小矩阵首叶观察范围不足，拒绝截断计数')
    root = struct.unpack('<I',memory_read(guest,address,4))[0]
    bodies = []
    if root:
        branch = struct.unpack('<I',memory_read(guest,root,4))[0]
        block = struct.unpack('<I',memory_read(guest,branch,4))[0]
        used,pages,history = struct.unpack('<3I',memory_read(guest,block,12))
        result.update(used_mask=used,block_pages=pages,history_mask=history)
        for index in range(32):
            if used&(1<<index):
                bodies.append(dict(id=index,bytes_hex=memory_read(guest,block+64+index*result['stride'],result['stride']).hex()))
    result['live_records'] = bodies
    if len(bodies)!=result['objects']:
        raise AssertionError('真实位图和对象数量不符')
    return result


def kernel_snapshot(guest,symbols):
    start = len(guest.debug)
    guest.vm.pipes['debug'].write(b'hello\nhalt\n')
    guest.wait(rb'STOP vector=[0-9a-f]+ eip=[0-9a-f]+\r\n',start,debug=True)
    try:
        handoff = memory_read(guest,symbols['snapshot'],256)
        if zlib.crc32(handoff[:252])!=struct.unpack_from('<I',handoff,252)[0]:
            raise AssertionError('实际主核交接快照CRC失败')
        result = dict(handoff_hex=handoff.hex(),boot_flags=struct.unpack_from('<I',handoff,12)[0],
                      boot_path=handoff[96:160].split(b'\0',1)[0].decode('ascii'),
                      core_payload_sha256=handoff[64:96].hex(),pools={})
        for name in ('residents','allocations','callbacks'):
            result['pools'][name] = pool_snapshot(guest,symbols[name])
        result['scene_owner'] = struct.unpack('<I',memory_read(guest,symbols['scene_owner'],4))[0]
        result['irq_heads'] = list(struct.unpack('<16I',memory_read(guest,symbols['irq_heads'],64)))
        result['service_head'] = struct.unpack('<I',memory_read(guest,symbols['service_head'],4))[0]
        if 'ata_diagnostic' in symbols:
            result['ata_diagnostic'] = list(struct.unpack('<16I',memory_read(guest,symbols['ata_diagnostic'],64)))
        return result
    finally:
        position = len(guest.debug)
        guest.vm.pipes['debug'].write(b'cont\n')
        guest.wait(rb'OK resume\r\n',position,debug=True)
        guest.client.checked(10)


def check_pools(snapshot,spec):
    pools = snapshot['pools']
    expected = {'residents':(spec['loaded'],3),'allocations':(1,3),'callbacks':(2,3)}
    for name,(objects,pages) in expected.items():
        if pools[name]['objects']!=objects or pools[name]['pages']!=pages:
            raise AssertionError('模块动态池未按实际存活资源回收：'+name)
    numbers,images,allocated = [],0,0
    for entry in pools['residents']['live_records']:
        body = bytes.fromhex(entry['bytes_hex'])
        base,pages,image,number,state,allocation_head,callback_head = struct.unpack_from('<7I',body,64)
        numbers.append(number);images += pages*4096
        if state!=2 or not base or image>pages*4096:
            raise AssertionError('常驻映像状态/页数不符')
        if number!=20 and (allocation_head or callback_head):
            raise AssertionError('失败回调/分配链污染其它常驻者')
    expected_numbers = sorted([20,100]+([10] if 10 in spec['order'] else []))
    if sorted(numbers)!=expected_numbers:
        raise AssertionError('错误常驻编号或失败模块遗留')
    owner_id = next(entry['id']+1 for entry in pools['residents']['live_records']
                    if struct.unpack_from('<I',bytes.fromhex(entry['bytes_hex']),76)[0]==20)
    for entry in pools['allocations']['live_records']:
        address,pages,owner = struct.unpack_from('<3I',bytes.fromhex(entry['bytes_hex']))
        if owner!=owner_id or pages!=2 or not address:
            raise AssertionError('辅助页归属/数量不符')
        allocated += pages*4096
    kinds,lines = [],[]
    for entry in pools['callbacks']['live_records']:
        values = struct.unpack_from('<13I',bytes.fromhex(entry['bytes_hex']))
        token,owner,kind,line = values[:4]
        if token!=entry['id']+1 or owner!=owner_id or not values[11]:
            raise AssertionError('回调令牌/归属/入口不符')
        kinds.append(kind);lines.append(line)
    if sorted(kinds)!=[1,2] or any(lines):
        raise AssertionError('PIT/周期服务注册不符')
    if not snapshot['irq_heads'][0] or any(snapshot['irq_heads'][1:]) or not snapshot['service_head']:
        raise AssertionError('共享IRQ/服务头不符')
    return dict(image_bytes=images,auxiliary_bytes=allocated,pool_bytes=sum(pool['pages'] for pool in pools.values())*4096,
                exact_live_module_pages=(images+allocated)//4096+sum(pool['pages'] for pool in pools.values()))


def read_order(guest,spec):
    body = guest.get_bytes('/TMP/M10ORDER','initialization-order')
    if len(body)!=12*len(spec['order']):
        raise AssertionError('初始化次数或输出长度不符')
    rows = [list(struct.unpack_from('<3I',body,offset)) for offset in range(0,len(body),12)]
    if [row[0] for row in rows]!=spec['order'] or any(row[1]!=1 for row in rows):
        raise AssertionError('编号排序/BSS初值/每开机一次初始化失败')
    return rows


def service_sample(guest,report,label):
    run_case(guest,report,'stable-service-snapshot-'+label,'/TMP/M10CORE.SCX snapshot')
    return struct.unpack('<6I',guest.get_bytes('/TMP/M10SNAP','service-'+label))


def run_profile(boot,data,directory,profile,spec,symbols,core_sha):
    report = dict(status='RUNNING',profile=profile,scope='DECLARED_USER_SIGNED_CORE_CASES_ONLY',cases=[],screenshots=[])
    with QemuSession(boot,data,directory,'tcg',True,audio=False,network='none',memory=256) as vm:
        guest = Guest(vm)
        trace = (vm.out/'management-metadata.jsonl').open('x',encoding='utf-8')
        def record(direction,kind,sequence,payload):
            # 只记协议元数据与回复状态；不写请求正文、令牌或凭据。
            event = dict(monotonic=time.monotonic(),direction=direction,kind=kind,sequence=sequence,
                         payload_bytes=len(payload),pending=guest.client.pending)
            if direction=='receive' and kind&0x8000 and len(payload)>=4:
                event['result'] = struct.unpack_from('<i',payload)[0]
            trace.write(json.dumps(event)+'\n');trace.flush()
        input_decoder = Decoder(lambda kind,sequence,payload:record('send',kind,sequence,payload))
        original_write = vm.pipes['management'].write
        def traced_write(data):
            input_decoder.feed(data)
            return original_write(data)
        vm.pipes['management'].write = traced_write
        original_consume = guest.client.decoder.consume
        def traced_receive(kind,sequence,payload):
            record('receive',kind,sequence,payload)
            return original_consume(kind,sequence,payload)
        guest.client.decoder.consume = traced_receive
        try:
            if profile=='both-main-bad':
                guest.wait(rb'PRIMARY REJECT 5\r\nRECOVERY REJECT 5\r\nCORE LOADER FAIL 3\r\n',timeout=30,debug=True)
                # 让真实VM继续运行1秒，再核没有进入任何MAIN或EXT。
                time.sleep(1)
                if b'CORE START ' in guest.debug or b'CORE EXT ' in guest.debug:
                    raise AssertionError('坏映像仍被执行')
                report['cases'].append(dict(name='both-payloads-rejected-before-jump',status='PASS'))
            else:
                guest.wait(b'CORE START '+spec['boot_path'].encode()+rb'\r\n',timeout=30,debug=True)
                wait_first_desktop(guest,symbols,report)
                guest.connect()
                if profile=='recovery':
                    guest.wait(rb'PRIMARY REJECT 5\r\n',timeout=30,debug=True)
                fixture = (ROOT/'tests/m10/M10CORE.C').read_bytes()
                guest.put_bytes(fixture,'/TMP/M10CORE.C','CORE-public-probe')
                report['fixture_source_sha256'] = digest_payload(fixture)
                run_case(guest,report,'native-module-ABI-probe','s3c /TMP/M10CORE.C /TMP/M10CORE.SCX',timeout=360)
                run_case(guest,report,'old-module-bounds-and-no-hot-init','/TMP/M10CORE.SCX',
                         (b'PASS MODULEINFO exact 32 bytes',b'PASS CORE and RECOVERY cannot be manually initialized'))
                info = struct.unpack('<8I',guest.get_bytes('/TMP/M10MODULE','module-info'))
                if info[1]!=spec['loaded'] or info[2]!=spec['loaded']*4096 or info[4]!=spec['loaded'] or info[5]!=spec['failed']:
                    raise AssertionError('实际模块数量/映像页/启动成功失败计数不符')
                report['module_info'] = list(info)
                report['initialization_order'] = read_order(guest,spec)
                before = service_sample(guest,report,'before')
                if before[:3]!=(1,20,1) or not before[3] or not before[4]:
                    raise AssertionError('真实PIT/常驻服务没有执行')
                deadline = time.monotonic()+20
                while True:
                    after = service_sample(guest,report,'after')
                    if after[3]>before[3] and after[4]>before[4] and after[5]>before[5]:
                        break
                    if time.monotonic()>=deadline:
                        raise TimeoutError('常驻IRQ/服务没有继续运行')
                    time.sleep(.1)
                report['service_before'],report['service_after'] = list(before),list(after)
                run_case(guest,report,'failed-init-does-not-run-service','test ! -e /TMP/M10FAILED')
                if read_order(guest,spec)!=report['initialization_order']:
                    raise AssertionError('服务轮错误重新初始化模块')
                events = re.findall(rb'CORE EXT (\S+) (\S+)(?: (-?[0-9]+))?\r\n',bytes(guest.debug))
                actual = {path.decode():(state.decode(),int(error) if error else 0) for path,state,error in events}
                expected = dict(spec['rejected'])
                for name in ('A20-SERVICE.SKM','WALL100.SKM'):
                    expected['SYS/CORE/'+name] = ('STARTED',0)
                if 10 in spec['order']:
                    expected['SYS/CORE/Z10-INIT.SKM'] = ('STARTED',0)
                if actual!=expected or len(events)!=len(expected):
                    raise AssertionError('实际扫描/拒绝原因/次数不符：'+repr(actual))
                report['extension_events'] = actual
                snapshot = kernel_snapshot(guest,symbols)
                if snapshot['boot_path']!=spec['boot_path'] or snapshot['boot_flags']!=(1 if profile=='recovery' else 0) or snapshot['core_payload_sha256']!=core_sha:
                    raise AssertionError('实际主核交接路径/恢复标志/正文摘要不符')
                report['kernel_snapshot'] = snapshot
                report['module_resource_ledger'] = check_pools(snapshot,spec)
                report['cases'].append(dict(name='exact-live-pools-pages-owners-and-no-failed-callbacks',status='PASS'))
                code,names,_,_,_ = guest.run('lsmod')
                live = set(names.decode('utf-8').splitlines())
                expected_live = {path for path,state in expected.items() if state==('STARTED',0)}
                if code or live!=expected_live:
                    raise AssertionError('真实常驻列表不符')
                report['resident_paths'] = sorted(live)
            vm.hmp('sendkey esc')
            picture = vm.out/'core-result.ppm'
            vm.hmp('screendump "'+picture.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(picture))
            report['status'] = 'DECLARED_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,message=str(error),traceback=traceback.format_exc()))
            try:
                if vm.process.poll() is None:
                    report['failure_qmp_status'] = vm.qmp('query-status')
                    vm.qmp('stop')
                    report['failure_cpu_registers'] = vm.hmp('info registers')
                    # HMP只读当前匹配ELF的公开内部计数/队列头，不查看
                    # 请求正文、用户凭据或fw_cfg熵。停机采样保持同一现场。
                    report['failure_uart_management_counters'] = {}
                    for name,offset,words in (('serial_ports',8232,10),('last_sequence',0,8),('fs_faulted',0,1),('ata_diagnostic',0,16)):
                        if name in symbols:
                            report['failure_uart_management_counters'][name] = vm.hmp(f'xp /{words}wx 0x{symbols[name]+offset:08x}')
            except Exception as diagnostic_error:
                report['failure_diagnostic_error'] = str(diagnostic_error)
            try:
                if vm.process.poll() is None:
                    picture = vm.out/'core-failure.ppm'
                    vm.hmp('screendump "'+picture.as_posix()+'"')
                    report['screenshots'].append(png_from_ppm(picture))
            except Exception as capture_error:
                report['capture_error'] = str(capture_error)
            raise
        finally:
            original_error = sys.exc_info()[1]
            try:
                try:
                    guest.close()
                except Exception as close_error:
                    report['guest_cleanup_error'] = str(close_error)
                    if original_error is None:
                        raise
                    original_error.add_note('管理会话清理同时失败：'+str(close_error))
            finally:
                trace.close()
                report['source_unchanged'] = sha(data)==vm.report['sources']['sanddata']['sha256']
                checkpoint(vm.out/'core.json',report)
    return report


def reuse_verified(previous,index,profile,source,boot,spec,identity):
    matrix_path = previous/'core-matrix.json'
    matrix = json.loads(matrix_path.read_text(encoding='utf-8'))
    if any(matrix.get(key)!=value for key,value in identity.items()):
        raise ValueError('旧证据主核/符号/用户公钥不同，不能复用')
    if len(matrix['disks'])<index:
        return None
    disk = matrix['disks'][index-1]
    if Path(disk['source']).resolve()!=source.resolve():
        raise ValueError('旧证据来源序号不一致')
    matches = [outcome for outcome in disk['profiles'] if outcome['profile']==profile]
    if not matches:
        return None
    if len(matches)!=1:
        raise ValueError('旧证据重复profile')
    outcome = matches[0]
    if outcome['status']!='DECLARED_CASES_PASS' or not outcome['source_unchanged'] or any(case['status']!='PASS' for case in outcome['cases']):
        return None
    if outcome.get('fixture_source_sha256')!=sha(ROOT/'tests/m10/M10CORE.C'):
        raise ValueError('旧证据客体探针源码不同')
    preparation = json.loads((previous/f'disk-{index}-{profile}-input.preparation.json').read_text(encoding='utf-8'))
    prepared = previous/f'disk-{index}-{profile}-input.img'
    session = json.loads((previous/f'disk-{index}-{profile}/session.json').read_text(encoding='utf-8'))
    if (preparation['source_sha256']!=sha(source) or not preparation['source_unchanged']
            or preparation['destination_sha256']!=sha(prepared) or session['exit_code']!=0
            or session['sources']['sandcore']['sha256']!=sha(boot)
            or session['sources']['sanddata']['sha256']!=preparation['destination_sha256']
            or not all(session['source_unchanged'].values())):
        raise ValueError('旧输入/来源/loader摘要或终态改变，不能复用')
    with ExitStack() as stack:
        blob = mapped(stack,prepared)
        records = read_image(blob,False)
        stack.callback(lambda: release_views(records))
        actual = {record.name:bytes(record.payload) for record in records.values() if record.payload is not None
                  and (record.name.startswith('SYS/CORE/') or record.name.startswith('SYS/RECOVERY/'))
                  and record.name.upper().endswith(('.SKM','.SCX'))}
        expected = dict(spec['files'], **{'SYS/CORE/CORE.SKM':spec['primary'],'SYS/RECOVERY/CORE.SKM':spec['recovery']})
        # 非测试SCX（LOGIN/IMAGE）允许原字节保留，测试映像集合与正文
        # 必须一致。复用有明确哈希/终态，不凭旧标题或部分输出宣称PASS。
        tested = {name:data for name,data in actual.items() if name in expected or name.upper().endswith('.SKM')}
        if tested!=expected:
            raise ValueError('旧签署正文/受控文件集合与当前用例不同')
    outcome = dict(outcome)
    outcome['reused_verified_evidence'] = dict(matrix=str(matrix_path.resolve()),matrix_sha256=sha(matrix_path),
                                             input_sha256=sha(prepared),scope='Same core key loader source fixture and exact test files')
    return outcome


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot',type=Path,required=True)
    parser.add_argument('--data',type=Path,action='append',required=True)
    parser.add_argument('--core',type=Path,required=True)
    parser.add_argument('--symbols',type=Path,required=True)
    parser.add_argument('--receipt',type=Path,required=True)
    parser.add_argument('--legacy-wall',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--profiles',choices=PROFILE,nargs='+',default=list(PROFILE))
    parser.add_argument('--openssl',default='openssl')
    parser.add_argument('--reuse-verified',type=Path,help='只复用同主核/键/loader/源码/盘摘要和完整PASS的既有profile')
    args = parser.parse_args()
    if os.name!='nt' or len(args.data)<2 or len(set(path.resolve(strict=True) for path in args.data))!=len(args.data):
        parser.error('Windows Python与至少两不同来源盘')
    key,signed = artifacts(args.receipt,args.openssl)
    main_blob,legacy = args.core.read_bytes(),args.legacy_wall.read_bytes()
    check_main(main_blob)
    if legacy[:8]!=b'SKM1MIO\0' or key not in main_blob[128:]:
        raise ValueError('需要原SKM1壁纸及包含用户公钥的实际主核')
    symbols = symbol_table(args.symbols)
    args.out.mkdir(parents=True,exist_ok=False)
    report = dict(status='RUNNING',scope='DECLARED_USER_SIGNED_CORE_CASES_NOT_WHOLE_M10A1',disks=[],
                  profiles=args.profiles,complete_profiles=set(args.profiles)==set(PROFILE),
                  core_sha256=digest_payload(main_blob),symbols_sha256=sha(args.symbols),
                  public_key_sha256=digest_payload(key),private_key_access='NONE')
    identity = {key:report[key] for key in ('core_sha256','symbols_sha256','public_key_sha256')}
    checkpoint(args.out/'core-matrix.json',report)
    try:
        for index,source in enumerate(args.data,1):
            disk_report = dict(source=str(source.resolve()),profiles=[])
            report['disks'].append(disk_report)
            checkpoint(args.out/'core-matrix.json',report)
            for profile in args.profiles:
                spec = specification(profile,signed,main_blob,legacy)
                if args.reuse_verified:
                    prior = reuse_verified(args.reuse_verified,index,profile,source,args.boot,spec,identity)
                    if prior is not None:
                        disk_report['profiles'].append(prior)
                        checkpoint(args.out/'core-matrix.json',report)
                        print(f'disk-{index}/{profile}: REUSED_VERIFIED_PASS',flush=True)
                        continue
                prepared = args.out/f'disk-{index}-{profile}-input.img'
                preparation = prepare(source,prepared,spec)
                (prepared.with_suffix('.preparation.json')).write_text(json.dumps(preparation,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
                print(f'disk-{index}/{profile}: PREPARED',flush=True)
                outcome = run_profile(args.boot,prepared,args.out/f'disk-{index}-{profile}',profile,spec,symbols,main_blob[72:104].hex())
                disk_report['profiles'].append(outcome)
                checkpoint(args.out/'core-matrix.json',report)
                print(f'disk-{index}/{profile}: '+outcome['status'],flush=True)
            if {'positive','without-failed-init'} <= set(args.profiles):
                results = {item['profile']:item for item in disk_report['profiles']}
                with_failure = results['positive']['kernel_snapshot']['pools']
                without_failure = results['without-failed-init']['kernel_snapshot']['pools']
                for name in with_failure:
                    if any(with_failure[name][field]!=without_failure[name][field] for field in ('objects','pages','stride','used_mask')):
                        raise AssertionError('失败初始化留下额外模块对象/池页：'+name)
                disk_report['failed_init_matching_live_resource_ledger'] = True
                checkpoint(args.out/'core-matrix.json',report)
        report['status'] = 'DECLARED_CASES_PASS'
    except BaseException as error:
        report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,message=str(error),traceback=traceback.format_exc()))
        raise
    finally:
        checkpoint(args.out/'core-matrix.json',report)


if __name__ == '__main__':
    main()
