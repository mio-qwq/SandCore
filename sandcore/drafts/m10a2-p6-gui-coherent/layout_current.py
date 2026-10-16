"""供Root显式提取当前i386布局；本轮交接只准备脚本，不执行构建。"""
from pathlib import Path
import argparse
import hashlib
import json
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import m10a2_shell_inspect as inspect


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if not out.is_relative_to(ROOT/'build') or not out.name.startswith('m10a2-p6-gui-coherent-layout-') or out.exists():
        raise ValueError('需要新的本任务布局输出目录')
    source_names = ('kernel/task_store.h', 'kernel/task.h', 'kernel/objpool.h',
                    'user/scui/SCUIPRIVATE.H', 'user/scshell/desktop/DESKTOP.H')
    before = {name: sha(ROOT/name) for name in source_names}
    extractor = ROOT/'tools/m10a2_shell_inspect.py'
    extractor_sha = sha(extractor)
    fields = dict(inspect.FIELDS)
    try:
        # 复用原sizeof/offsetof真实编译，不用旧values配上新头摘要。
        inspect.FIELDS.update(record_name='O(task_record_t,task)+O(task_t,name)',
                             record_name_bytes='sizeof(((task_t *)0)->name)')
        inspect.build_layout(ROOT, out)
    finally:
        inspect.FIELDS.clear()
        inspect.FIELDS.update(fields)
    layout = json.loads((out/'layout.json').read_text(encoding='utf-8'))
    after = {name: sha(ROOT/name) for name in source_names}
    if before != after or layout['headers'] != before or sha(extractor) != extractor_sha:
        raise ValueError('实际提取期间布局源或提取器发生变化')
    files = {name: dict(sha256=sha(out/name), bytes=(out/name).stat().st_size)
             for name in ('layout.json', 'layout.c', 'layout.o', 'layout.bin', 'build.log')}
    proof = dict(schema=1, status='ACTUAL_CURRENT_I386_LAYOUT_GENERATED_NO_GUEST',
                 source_unchanged=True, headers=before, files=files, command=layout['command'],
                 extractor=dict(relative='tools/m10a2_shell_inspect.py', sha256=extractor_sha),
                 scope='HOST_STRUCT_LAYOUT_ONLY_NOT_COMPONENT_BUILD_OR_GUEST_EXECUTION')
    (out/'layout-proof.json').write_text(json.dumps(proof, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(status=proof['status'], layout_report=str(out/'layout-proof.json'),
                         sha256=sha(out/'layout-proof.json')), ensure_ascii=False))


if __name__ == '__main__':
    main()
