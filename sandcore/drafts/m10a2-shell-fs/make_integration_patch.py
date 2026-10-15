"""只生成隔离patch，不写现有源码，不构建/运行任何SandCore程序。"""
from pathlib import Path
import difflib
import hashlib
import json

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
original = {}
desired = {}


def source(name):
    path = ROOT / name
    body = path.read_text(encoding='utf-8')
    original[name] = body
    desired[name] = body
    return body


def change(name, before, after):
    if desired[name].count(before) != 1:
        raise ValueError('集成锚点不是唯一原正文：' + name + '\n' + before[:160])
    desired[name] = desired[name].replace(before, after, 1)


def function(name, begin, end, edits):
    body = desired[name]
    first = body.index(begin)
    last = body.index(end, first) if end else len(body)
    part = body[first:last]
    for before, after in edits:
        if part.count(before) != 1:
            raise ValueError('函数集成锚点不是唯一原正文：' + begin + '\n' + before[:160])
        part = part.replace(before, after, 1)
    desired[name] = body[:first] + part + body[last:]


fs = 'kernel/fs.c'
source(fs)
change(fs, '#include "sc32_file.h"', '#include "sc32_file.h"\n#include "fs_lease.h"')
change(fs, 'char name[64];entry_t candidate,previous;', 'char name[64];entry_t candidate,previous,draft;')
change(fs, 'u32 relocated,complete,accepted,uncertain;',
       'u32 relocated,complete,accepted,uncertain;\n    u32 unpublished,invalidated,lease_head,flushed_revision;')
change(fs, 'enum { N_OPEN=1,N_WRITE,N_TRUNCATE,N_FLUSH };',
       'enum { N_OPEN=1,N_WRITE,N_TRUNCATE,N_FLUSH };\n#include "fs_lease_private.inc"')
change(fs, 'if(fs_faulted || native_meta_owner || native_directory_sequence==0xffffffffu)return -1;',
       'if(fs_faulted || native_meta_owner || fs_lease_volume_locked() || native_directory_sequence==0xffffffffu)return -1;')
change(fs, 'static void release_directory(void)\n{', 'static void release_directory(void)\n{\n    lease_volume_reset();')
function(fs, 'int fs_write(', 'int fs_mkdir(', [
    ('if(native_meta_owner || busy(name))return -6;',
     'if(native_meta_owner || fs_lease_volume_locked() || fs_lease_path_blocked(name,FS_LEASE_CONTENT) || busy(name))return -6;')])
function(fs, 'int fs_mkdir(', 'int fs_remove(', [
    ('if(native_meta_owner)return -6;', 'if(native_meta_owner || fs_lease_volume_locked())return -6;'),
    ('if((u32)nfiles>=dir_capacity)return -1;',
     'if(fs_lease_path_blocked(name,FS_LEASE_TREE) || busy(name))return -6;\n    if((u32)nfiles>=dir_capacity)return -1;')])
function(fs, 'int fs_remove(', 'int fs_rename(', [
    ('if(native_meta_owner || busy(name))return -6;',
     'if(native_meta_owner || fs_lease_volume_locked() || fs_lease_path_blocked(name,FS_LEASE_TREE) || busy(name))return -6;')])
function(fs, 'int fs_rename(', 'int fs_list_dir(', [
    ('if(native_meta_owner || busy(old) || busy(next))return -6;',
     'if(native_meta_owner || fs_lease_volume_locked() || fs_lease_path_blocked(old,FS_LEASE_TREE)\n       || fs_lease_path_blocked(next,FS_LEASE_TREE) || busy(old) || busy(next))return -6;')])
function(fs, 'int fs_create_ex(', 'static int permission(', [
    ('if(native_meta_owner)return -6;', 'if(native_meta_owner || fs_lease_volume_locked())return -6;'),
    ('if(busy(name))return -6;', 'if(fs_lease_path_blocked(name,FS_LEASE_TREE) || busy(name))return -6;')])
function(fs, 'int fs_permissions(', 'static transaction_t *stream(', [
    ('if(native_meta_owner)return -6;', 'if(native_meta_owner || fs_lease_volume_locked())return -6;')])
function(fs, 'int fs_stream_begin(', 'int fs_stream_write(', [
    ('if(native_meta_owner || busy(name))return -6;',
     'if(native_meta_owner || fs_lease_volume_locked() || fs_lease_path_blocked(name,FS_LEASE_CONTENT) || busy(name))return -6;')])
function(fs, 'int fs_stream_write(', 'int fs_stream_abort(', [
    ('transaction_t *t=stream(owner,token);if(!t)return -1;',
     'transaction_t *t=stream(owner,token);if(!t)return -1;\n    if(fs_lease_path_blocked(t->name,FS_LEASE_CONTENT))return -6;')])
function(fs, 'int fs_stream_commit(', 'static native_file_t *native_object(', [
    ('if(native_meta_owner || native_busy(t->name))return -6;',
     'if(native_meta_owner || fs_lease_volume_locked() || fs_lease_path_blocked(t->name,FS_LEASE_CONTENT) || native_busy(t->name))return -6;')])
function(fs, 'static int native_index(', 'static int native_access(', [
    ('int i=find(n->name);', 'if(n->invalidated)return FS_N_STALE;\n    if(n->unpublished)return 0;\n    int i=find(n->name);')])
function(fs, 'static int native_access(', 'int fs_native_info(', [
    ('if(!task_owned(pid) || !n || !n->ready)return FS_N_STALE;',
     'if(!task_owned(pid) || !n || !n->ready || n->invalidated)return FS_N_STALE;\n    if(n->unpublished)return lease_private_access(n,pid,access);')])
function(fs, 'int fs_native_info(', 'static void native_pending_clear(', [
    ('entry_t *e=dirents+n->index;', 'entry_t *e=native_current(n);'),
    ('(e->generation>published_object_generation?FS_NATIVE_DIRTY:0)',
     '(n->unpublished || e->generation>published_object_generation?FS_NATIVE_DIRTY:0)')])
function(fs, 'void fs_native_cancel(', 'void fs_native_close(', [
    ('if(native_meta_owner==n->id)native_meta_owner=native_meta_sector=0;',
     'if(n->unpublished){lease_private_discard(n);return;}\n    if(native_meta_owner==n->id)native_meta_owner=native_meta_sector=0;')])
function(fs, 'static void native_failed(', 'static void native_serialize(', [
    ('if(n->reserved_count)allocated(n->reserved_start,n->reserved_count,0);',
     'if(n->unpublished && n->reserved_count){lease_record_t *r=lease_get(n->lease_head);\n        if(r){r->garbage_start=n->reserved_start;r->garbage_count=n->reserved_count;lease_work_add(r);}}\n    else if(n->reserved_count)allocated(n->reserved_start,n->reserved_count,0);')])
function(fs, 'static int native_publish_start(', 'static int native_publish_step(', [
    ('if(native_meta_owner)return native_meta_owner==n->id?FS_N_AGAIN:FS_N_BUSY;',
     'if(n->unpublished)return lease_private_commit(n);\n    if(fs_lease_volume_locked())return FS_N_AGAIN;\n    if(native_meta_owner)return native_meta_owner==n->id?FS_N_AGAIN:FS_N_BUSY;')])
function(fs, 'static int native_publish_step(', 'void fs_native_poll(', [
    ('int pid=(int)n->owner;',
     'int pid=(int)n->owner;\n    if(lease_native_guard(n,pid,n->writer,FS_NATIVE_WRITE)){\n        native_failed(n,FS_N_BUSY,0);native_completed(n);return FS_N_BUSY;\n    }')])
function(fs, 'static void native_pending_start(', 'int fs_native_open(', [
    ('n->previous=n->candidate=dirents[n->index];', 'n->previous=n->candidate=*native_current(n);')])
function(fs, 'int fs_native_open(', 'int fs_native_read(', [
    ('|| ((flags&(FS_NATIVE_CREATE|FS_NATIVE_TRUNCATE|FS_NATIVE_APPEND)) && !(flags&FS_NATIVE_WRITE)))return FS_N_INVALID;',
     '|| ((flags&(FS_NATIVE_CREATE|FS_NATIVE_TRUNCATE|FS_NATIVE_APPEND)) && !(flags&FS_NATIVE_WRITE)))return FS_N_INVALID;\n    if(fs_lease_volume_locked())return FS_N_AGAIN;\n    if(lease_native_open_blocked(name,flags))return FS_N_BUSY;'),
    ('if(!candidate->ready || candidate->pending)return FS_N_AGAIN;',
     'if(candidate->unpublished || candidate->invalidated)return FS_N_BUSY;\n        if(!candidate->ready || candidate->pending)return FS_N_AGAIN;')])
function(fs, 'int fs_native_read(', 'static int native_growth_start(', [
    ('entry_t *e=dirents+n->index;', 'entry_t *e=native_current(n);')])
function(fs, 'static int native_bytes_write(', 'int fs_native_write(', [
    ('candidate?n->candidate.start_lba:dirents[n->index].start_lba;',
     'candidate?n->candidate.start_lba:native_current(n)->start_lba;')])
function(fs, 'int fs_native_write(', 'int fs_native_truncate(', [
    ('entry_t *e=dirents+n->index;',
     'if(fs_lease_volume_locked())return FS_N_AGAIN;\n    error=lease_native_guard(n,pid,writer,FS_NATIVE_WRITE);if(error)return error;\n    entry_t *e=native_current(n);'),
    ('if(*done || n->uncertain){e->generation=generation;live_dirty=1;native_directory_sequence++;}',
     'if(*done || n->uncertain){e->generation=generation;live_dirty=1;n->flushed_revision=0;\n                if(!n->unpublished)native_directory_sequence++;}')])
function(fs, 'int fs_native_truncate(', 'int fs_native_flush(', [
    ('if(length>fs_disk_sectors*512u)return FS_N_RANGE;',
     'if(fs_lease_volume_locked())return FS_N_AGAIN;\n    error=lease_native_guard(n,pid,writer,FS_NATIVE_WRITE);if(error)return error;\n    if(length>fs_disk_sectors*512u)return FS_N_RANGE;'),
    ('if(length==dirents[n->index].size)return 0;', 'if(length==native_current(n)->size)return 0;'),
    ('entry_t *e=dirents+n->index;', 'entry_t *e=native_current(n);')])
function(fs, 'int fs_native_flush(', 'u32 fs_native_directory_epoch(', [
    ('if(n->pending){',
     'if(fs_lease_volume_locked())return FS_N_AGAIN;\n    error=lease_native_guard(n,pid,writer,FS_NATIVE_WRITE);if(error)return error;\n    if(n->unpublished){\n        if(n->pending)return FS_N_BUSY;\n        if(ata_flush())return FS_N_IO;\n        n->flushed_revision=n->draft.generation;return 0;\n    }\n    if(n->pending){')])
desired[fs] += '\n#include "fs_lease.inc"\n'

file = 'kernel/sc32_file.c'
source(file)
change(file, '#include "fs.h"', '#include "fs.h"\n#include "fs_lease.h"')
change(file, 'u32 native_reference,closing,cursor,epoch;', 'u32 native_reference,closing,cursor,epoch,lease,lease_metadata_only;')
change(file, '        drop(h);o=objpool_get(&owners,pid);if(!o)return 1;',
       '        if(h->lease && !fs_lease_detach_step(pid,h->id,h->lease))return 0;\n        drop(h);o=objpool_get(&owners,pid);if(!o)return 1;')
function(file, 'static int duplicate_file(', 'static int standard_file(', [
    ('file_description_t *d=description(original);',
     'if(original->lease)return SC32_E_BUSY;\n    if(original->lease_metadata_only)return SC32_E_ACCESS;\n    file_description_t *d=description(original);')])
function(file, 'static int directory(', 'int sc32_file_call(', [
    ('if(!fs_access(pid,name,FS_ACCESS_READ))return SC32_E_ACCESS;',
     'if(fs_lease_path_blocked(name,FS_LEASE_READ_OPEN))return SC32_E_BUSY;\n        if(!fs_access(pid,name,FS_ACCESS_READ))return SC32_E_ACCESS;'),
    ('if(operation==SC32_D_CLOSE){if(q[3] || !reserved(q,4))return SC32_E_INVALID;drop(h);return publish(pid,output,&r,sizeof(r));}',
     'if(operation==SC32_D_CLOSE){\n        if(q[3] || !reserved(q,4))return SC32_E_INVALID;\n        if(h->lease && !fs_lease_detach_step(pid,h->id,h->lease))return SC32_E_BUSY;\n        drop(h);return publish(pid,output,&r,sizeof(r));\n    }\n    if(h->lease_metadata_only)return SC32_E_ACCESS;')])
function(file, 'int sc32_file_call(', None, [
    ('file_description_t *d=description(h);if(!d || !d->ready)return SC32_E_BUSY;',
     'file_description_t *d=description(h);if(!d || !d->ready)return SC32_E_BUSY;\n    if(h->lease_metadata_only && operation!=SC32_F_QUERY && operation!=SC32_F_CLOSE)return SC32_E_ACCESS;'),
    ('if(operation==SC32_F_DUPLICATE)return duplicate_file(pid,h,output);',
     'if(operation==SC32_F_DUPLICATE)return duplicate_file(pid,h,output);\n    if(operation==SC32_F_CLOSE && h->lease_metadata_only){\n        if(h->lease && !fs_lease_detach_step(pid,h->id,h->lease))return SC32_E_BUSY;\n        file_reply(pid,h,d,&r,0,0);drop(h);r.handle=0;return publish(pid,output,&r,sizeof(r));\n    }\n    if(operation==SC32_F_CLOSE && h->lease){\n        int complete=fs_lease_file_private(d->object) || fs_lease_close_committed(h->lease,d->object);\n        if(!fs_lease_detach_step(pid,h->id,h->lease))return SC32_E_BUSY;\n        if(complete){file_reply(pid,h,d,&r,0,0);drop(h);r.handle=0;return publish(pid,output,&r,sizeof(r));}\n    }')])
desired[file] += '\n#include "sc32_file_lease.inc"\n'

task = 'kernel/task.c'
source(task)
change(task, '#include "sc32_file.h"', '#include "sc32_file.h"\n#include "sc32_lease.h"')
change(task, '        else frame[11]=(u32)sc32_core_call(current,frame[8],frame[10],frame[9]);',
       '        else if(frame[8]>=0x700u && frame[8]<0x720u)\n            frame[11]=(u32)sc32_lease_call(current,frame[8],frame[10],frame[9]);\n        else frame[11]=(u32)sc32_core_call(current,frame[8],frame[10],frame[9]);')
change(task, '        ){p->queue=QUEUE_ZOMBIE;p->zombie_next=zombie_head;zombie_head=owner;}\n        else{',
       '        ) && sc32_lease_stop_step(owner)){p->queue=QUEUE_ZOMBIE;p->zombie_next=zombie_head;zombie_head=owner;}\n        else{')
# 再把条件括号围住，保证drain仅在原终点之后调用，不在revoke barrier前等GC。
change(task, '        if(p->cleanup_phase>=\n#ifdef M10_DISK_CORE\n            10\n#else\n            9\n#endif\n        ) && sc32_lease_stop_step(owner))',
       '        if((p->cleanup_phase>=\n#ifdef M10_DISK_CORE\n            10\n#else\n            9\n#endif\n        ) && sc32_lease_stop_step(owner))')

main = 'kernel/main.c'
source(main)
change(main, '#include "sc32_broker.h"', '#include "sc32_broker.h"\n#include "sc32_lease.h"')
change(main, '        fs_native_poll();', '        fs_native_poll();\n        sc32_lease_poll();')

core = 'kernel/sc32_core.c'
source(core)
change(core, '#include "sc32_drop.h"', '#include "sc32_drop.h"\n#include "fs_lease.h"')
change(core, '    result.max_path_bytes=SC32_PATH_CAPACITY;',
       '    if(fs_lease_available())result.capabilities_hi|=SC32_CAP_LEASES_HI;\n    result.max_path_bytes=SC32_PATH_CAPACITY;')

make = 'Makefile'
source(make)
change(make, 'sc32_broker.o sc32_drop.o', 'sc32_broker.o sc32_drop.o sc32_lease.o')
rules = 'm10a2.mk'
source(rules)
desired[rules] += '''
# 租约正文保持独立新selector，短步清理由可信task/main接线。
$(BUILD)/sc32_lease.o: kernel/sc32_lease.c kernel/sc32_lease.h kernel/fs_lease.h user/SC32LEASE.H user/leaseapi/SC32LEASEWIRE.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(BUILD)/fs.o: $(wildcard kernel/fs_lease*.inc) kernel/fs_lease.h user/SC32LEASE.H user/leaseapi/SC32LEASEWIRE.H
$(BUILD)/sc32_file.o: kernel/sc32_file_lease.inc kernel/fs_lease.h user/SC32LEASE.H
$(BUILD)/sc32-library.o: user/leaseapi/leaseapi.c user/leaseapi/SC32LEASEWIRE.H user/SC32LEASE.H
'''

api = 'user/sc32/SC32API.C'
source(api)
desired[api] += '\n#include "../leaseapi/leaseapi.c"\n'
header = 'user/SC32API.H'
source(header)
change(header, '#define SC32_ABI_MINOR 2u', '#define SC32_ABI_MINOR 3u')
change(header, '#define SC32_LIBRARY_VERSION 3u', '#define SC32_LIBRARY_VERSION 4u')
manifest = 'user/sc32/SC32API.json'
source(manifest)
value = json.loads(original[manifest])
value['abi_minor'] = 3
value['library_version'] = 4
exports = ('Acquire', 'OpenPinned', 'CreatePinned', 'Query', 'Snapshot', 'Publish',
           'Rename', 'Remove', 'Release', 'Cancel', 'Advance', 'SetMode')
value['exports'] += [dict(name='ScFileLease' + name, kind='function') for name in exports]
value['exports'].sort(key=lambda item: item['name'].encode('ascii'))
desired[manifest] = json.dumps(value, ensure_ascii=False, indent=2) + '\n'

parts = []
for name, before in original.items():
    parts += list(difflib.unified_diff(before.splitlines(True), desired[name].splitlines(True),
                                      fromfile='a/sandcore/' + name, tofile='b/sandcore/' + name))
# 新正文已经在隔离正式相对目录中，不再重复塞一份进patch。
# Root按new_files清单复制到正式目标后才应用这些必要现文件挂钩。
(HERE / 'integration.patch').write_text(''.join(parts), encoding='utf-8', newline='\n')
proof = dict(status='PATCH_SOURCE_ONLY_NOT_APPLIED_NOT_BUILT',
             expected_sources={name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                               for name in original},
             changes=list(original), new_files=[path.relative_to(HERE).as_posix()
                 for directory in ('kernel', 'user') for path in sorted((HERE / directory).rglob('*')) if path.is_file()])
(HERE / 'integration-inputs.json').write_text(json.dumps(proof, ensure_ascii=False, indent=2) + '\n',
                                             encoding='utf-8', newline='\n')
print('WROTE SOURCE PATCH ONLY; current source files were not written')
