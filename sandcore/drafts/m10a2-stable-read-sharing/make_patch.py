"""从当前只读前像生成稳定读共享的局部补丁，不应用、不编译、不启动VM。"""
from pathlib import Path
import ast
import difflib
import hashlib
import json

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BODY = {}
OLD = {}
NEW = {}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def source(name):
    if name not in OLD:
        BODY[name] = (ROOT / name).read_bytes()
        OLD[name] = BODY[name].decode('utf-8').replace('\r\n', '\n')
        NEW[name] = OLD[name]
    return NEW[name]


def edit(name, old, new):
    text = source(name)
    assert text.count(old) == 1, (name, old)
    NEW[name] = text.replace(old, new, 1)


def save(name, value):
    (HERE / name).write_bytes((json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))


def main():
    edit('kernel/fs_lease.h', '#define FS_LEASE_READ_OPEN 3u', '''#define FS_LEASE_READ_OPEN 3u
/* 只用于取得新稳定读：可与空闲READ_STABLE共存，绝不能借普通READ_OPEN
 * 同时放过写租约；不是公开selector或用户flags。 */
#define FS_LEASE_STABLE_OPEN 4u''')
    edit('kernel/fs_lease_index.inc', '''        if(covers && !lease_allowed(r,a,b,ns)){
            if(operation==FS_LEASE_READ_OPEN''', '''        if(covers && !lease_allowed(r,a,b,ns)){
            /* kind不会随Rename/Remove/SetMode改变；只放过真正空闲的读。
             * slot1是提交目标保留，PENDING/清理也不能被新读者插入。 */
            if(operation==FS_LEASE_STABLE_OPEN && !slot && r->kind==SC32_LEASE_READ_STABLE
               && r->handle_kind==1 && (r->state==SC32_LEASE_READY || r->state==SC32_LEASE_COMMITTED)
               && !r->cancel && !r->release && !r->queued && !r->stage
               && !(r->flags&(SC32_LEASE_FLAG_CANDIDATE|SC32_LEASE_FLAG_REVOKED|SC32_LEASE_FLAG_UNCERTAIN))){token=next;continue;}
            if(operation==FS_LEASE_READ_OPEN''')
    edit('kernel/fs_lease_open.inc', '    if(lease_conflict(b.path,directory?FS_LEASE_TREE:FS_LEASE_CONTENT,0,0,parent?parent->id:0)',
         '''    u32 conflict_operation=directory?FS_LEASE_TREE:q->kind==SC32_LEASE_READ_STABLE?FS_LEASE_STABLE_OPEN:FS_LEASE_CONTENT;
    if(lease_conflict(b.path,conflict_operation,0,0,parent?parent->id:0)''')
    edit('kernel/fs_lease_open.inc', '    if(lease_conflict(name,directory?FS_LEASE_TREE:FS_LEASE_CONTENT,0,0,parent?parent->id:0)',
         '''    u32 conflict_operation=directory?FS_LEASE_TREE:q->lease_kind==SC32_LEASE_READ_STABLE?FS_LEASE_STABLE_OPEN:FS_LEASE_CONTENT;
    if(lease_conflict(name,conflict_operation,0,0,parent?parent->id:0)''')
    name = 'docs/SC32-FILE.md'
    text = source(name)
    NEW[name] = text + '''
## 稳定读共享打开

READ_STABLE保证来源正文/名称稳定，不要求稳定读者之间独占。内核私有STABLE_OPEN仅供FILE稳定读Acquire/OpenPinned，允许空闲且有效的slot0 READ_STABLE共存；write lease、namespace、slot1目标保留和PENDING/取消/清理/异常记录仍冲突。普通READ_OPEN原语义不变，不能用于取得稳定性，因为它也允许普通读取写租约对象。

各读者分别持本人FILE/游标、owner PID/gen、精确object/revision和lease强引用。原R或METADATA_ONLY的META权限、精确版本、native pending、别名与ref溢出检查保持；写Acquire/OpenPinned与typed rename/remove/set-mode的唯一绑定不放宽。已有普通WRITE描述符的后续Write/Truncate/Flush仍由lease_native_guard检查读租约，不能绕过。没有新增公共调用、header布局、磁盘格式或权限。

旧fs_permissions仍按真正owner/管理者和原保护gate改权限，后续IO重验当前权限；READ_STABLE不屏蔽旧权限撤回。typed lease SetMode在存在其它稳定读时仍因unique/TREE冲突失败，不能把两者混称“所有chmod都被锁住”。本组仅隔离源码方案，尚未构建或客体验证。

修订记录：2026-10-16，稳定读共享打开与原权限/alias/writer边界合同；旧ABI及普通读取行为保持，待统一整合验证。
'''
    pins = {'status': 'SOURCE_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED', 'modified': {}}
    previous = HERE / 'PINS.json'
    if previous.exists():
        for name, record in json.loads(previous.read_text(encoding='utf-8'))['modified'].items():
            assert sha(BODY[name]) == record['before_sha256'], name
    chunks = []
    for name, text in NEW.items():
        result = text.encode('utf-8')
        path = HERE / 'overlay' / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(result)
        pins['modified'][name] = {'before_sha256': sha(BODY[name]), 'result_sha256': sha(result),
                                  'before_bytes': len(BODY[name]), 'result_bytes': len(result)}
        chunks.extend(difflib.unified_diff(OLD[name].splitlines(True), text.splitlines(True),
                                          fromfile='a/' + name, tofile='b/' + name, n=3))
    patch = ''.join(chunks).encode('utf-8')
    (HERE / 'stable-read-sharing.patch').write_bytes(patch)
    pins['patch_sha256'] = sha(patch)
    save('PINS.json', pins)
    references = {}
    for name in ('user/SCAPI.H', 'user/SC32LEASE.H', 'user/leaseapi/SC32LEASEWIRE.H',
                 'kernel/sc32_lease.c', 'kernel/sc32_file_lease.inc', 'kernel/fs.c', 'kernel/fs_lease_native.inc',
                 'kernel/fs_lease_operation.inc', 'kernel/fs_lease_commit.inc', 'kernel/fs_lease_cleanup.inc',
                 'kernel/interrupts.c', 'user/scsound/sound_api.c', 'docs/SCSHELL-DESIGN.md', 'docs/SCSOUND.md'):
        references[name] = {'sha256': sha((ROOT / name).read_bytes()), 'unchanged': True}
    save('SOURCE-PINS.json', references)
    assert all((ROOT / name).read_bytes() == data for name, data in BODY.items())
    assert 'FS_LEASE_STABLE_OPEN' not in OLD['kernel/fs_lease.h']
    assert NEW['kernel/fs_lease_open.inc'].count('FS_LEASE_STABLE_OPEN') == 2
    assert NEW['kernel/fs_lease_index.inc'].count('operation==FS_LEASE_STABLE_OPEN') == 1
    ast.parse(Path(__file__).read_text(encoding='utf-8'))
    inventory = {}
    for path in sorted(HERE.rglob('*')):
        if path.is_file() and path.name != 'DELIVERY.json':
            data = path.read_bytes()
            inventory[path.relative_to(HERE).as_posix()] = {'sha256': sha(data), 'bytes': len(data)}
    save('DELIVERY.json', {'status': pins['status'], 'files': inventory})
    print(json.dumps({'patch_sha256': sha(patch), 'delivery_sha256': sha((HERE / 'DELIVERY.json').read_bytes()),
                      'modified': len(pins['modified']), 'status': pins['status']}))


if __name__ == '__main__':
    main()
