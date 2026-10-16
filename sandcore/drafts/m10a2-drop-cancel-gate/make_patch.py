"""只读冻结Drop前像，输出独立两hunk；不应用，不编译，不改旧稿。"""
from pathlib import Path
import ast
import difflib
import hashlib
import json

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
FROZEN = HERE.parent / 'm10a2-drop-source-refresh'
PATH = 'user/scui/scui_drop_deferred.inc'
BASE_SHA = '9b37cdeefd5249c44b3200d0d13d09775efddbb058ea777e650ce3b891f94851'
DELIVERY_SHA = 'a86a156a34552d27f9c036e8a7a083afa18d9e4f91dacdaa00c5a005aadedf28'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save(path, value):
    path.write_bytes((json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))


def main():
    original = (FROZEN / 'overlay' / PATH).read_bytes()
    delivery = (FROZEN / 'DELIVERY.json').read_bytes()
    assert sha(original) == BASE_SHA and sha(delivery) == DELIVERY_SHA
    for name, record in json.loads(delivery)['files'].items():
        assert sha((FROZEN / name).read_bytes()) == record['sha256'], name
    text = original.decode('utf-8')
    before = 'static ScStatus ui_d2_source_step(UiDrag *d)\n{\n    if(d->deferred_stage==1){'
    after = '''static int ui_d2_source_retired(UiDrag *d)
{
    UiNode *source=ui_node(d->context,d->source),*root=ui_node(d->context,d->root);
    return !source || !root || (source->private_flags&UI_NODE_RETIRED) || (root->private_flags&UI_NODE_RETIRED);
}
static ScStatus ui_d2_source_step(UiDrag *d)
{
    /* 退役先从节点表移除，clear可等活跃回调；不让闲置channel挡住终止。 */
    if(ui_d2_source_retired(d))return SC_UI_E_CANCELLED;
    if(d->deferred_stage==1){'''
    assert text.count(before) == 1
    text = text.replace(before, after, 1)
    before = '''static int ui_d2_source_work(UiDrag *d)
{return !d->deferred_stage || (d->deferred_stage==1 && ui_d2_expired(&d->deadline)) || ui_d2_channel_work(d->channel,d->deferred_stage==1?sizeof(ScDragEnvelope):0);}'''
    after = '''static int ui_d2_source_work(UiDrag *d)
{
    /* 取消与节点退役本身需要清理，不得先用无数据channel把它们过滤掉。
     * 基础引用仍由原close释放；这里不直接置cancelled，避免跳过那次release。 */
    if(d->cancelled)return 1;
    if(d->deferred_stage && ui_d2_source_retired(d))return 1;
    return !d->deferred_stage || (d->deferred_stage==1 && ui_d2_expired(&d->deadline)) || ui_d2_channel_work(d->channel,d->deferred_stage==1?sizeof(ScDragEnvelope):0);
}'''
    assert text.count(before) == 1
    text = text.replace(before, after, 1)
    result = text.encode('utf-8')
    target = HERE / 'overlay' / PATH
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(result)
    patch = ''.join(difflib.unified_diff(original.decode('utf-8').splitlines(True), text.splitlines(True),
                                         fromfile='a/' + PATH, tofile='b/' + PATH, n=3)).encode('utf-8')
    assert len([line for line in patch.splitlines() if line.startswith(b'@@ ')]) == 2
    (HERE / 'drop-cancel-gate.patch').write_bytes(patch)
    references = {}
    for name in ('user/scui/ui_base.c', 'user/scui/ui_tree.c', 'user/scui/ui_data.c'):
        references[name] = sha((ROOT / name).read_bytes())
    save(HERE / 'PINS.json', {
        'status': 'SOURCE_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED',
        'apply_after': {'draft': FROZEN.name, 'delivery_sha256': DELIVERY_SHA},
        'path': PATH, 'before_sha256': BASE_SHA, 'result_sha256': sha(result),
        'patch_sha256': sha(patch), 'hunks': 2, 'read_only_references': references,
        'static': ['frozen delivery inventory unchanged', 'two unique exact anchors', 'two local hunks', 'no public header or import change']
    })
    assert (FROZEN / 'overlay' / PATH).read_bytes() == original
    assert sha((FROZEN / 'DELIVERY.json').read_bytes()) == DELIVERY_SHA
    ast.parse(Path(__file__).read_text(encoding='utf-8'))
    inventory = {}
    for path in sorted(HERE.rglob('*')):
        if path.is_file() and path.name != 'DELIVERY.json':
            data = path.read_bytes()
            inventory[path.relative_to(HERE).as_posix()] = {'sha256': sha(data), 'bytes': len(data)}
    save(HERE / 'DELIVERY.json', {'status': 'SOURCE_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED', 'files': inventory})
    print(json.dumps({'patch_sha256': sha(patch), 'result_sha256': sha(result),
                      'delivery_sha256': sha((HERE / 'DELIVERY.json').read_bytes())}))


if __name__ == '__main__':
    main()
