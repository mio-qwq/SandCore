#!/usr/bin/env python3
"""Prepare a reviewable unified P5 tree without modifying active P4 sources.

The reviewed FS, host and WM generators run sequentially against this isolated
tree. Their exact-anchor transformations compose on prior changes rather than
applying several stale patches over each other. This is not a build or runtime
acceptance. --out must be a new directory beneath this project's build/.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import runpy
import shutil

ROOT = Path(__file__).resolve().parents[1]
BASE = (
    'kernel/fs.c', 'kernel/sc32_file.c', 'kernel/task.c', 'kernel/main.c',
    'kernel/sc32_core.c', 'Makefile', 'm10a2.mk', 'user/sc32/SC32API.C',
    'user/SC32API.H', 'user/sc32/SC32API.json', 'kernel/task.h',
    'kernel/streams.c', 'kernel/streams.h', 'kernel/auth.h', 'kernel/auth.c',
    'kernel/session.c', 'kernel/sc32_broker.h', 'kernel/sc32_broker.c',
    'user/scui/SCUIPRIVATE.H', 'user/SCUI.H', 'user/scui/SCUI.json',
    'user/scui/ui_paint.c', 'user/scui/ui_dispatch.c', 'kernel/wm.c',
    'kernel/wm_native.inc', 'user/SC32GUI.H',
)
DRAFTS = ('fs', 'host', 'r3', 'worker', 'desktop')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def preserve_lines(before, after):
    old_lines = before.splitlines(True)
    normalized = [s.replace('\r\n', '\n') for s in old_lines]
    new_lines = after.splitlines(True)
    result = []
    for kind, a0, a1, b0, b1 in difflib.SequenceMatcher(a=normalized, b=new_lines, autojunk=False).get_opcodes():
        if kind == 'equal':
            result.extend(old_lines[a0:a1])
        else:
            anchor = old_lines[min(a0, len(old_lines)-1)] if old_lines else '\n'
            ending = '\r\n' if anchor.endswith('\r\n') else '\n'
            result.extend(s[:-1] + ending if s.endswith('\n') else s for s in new_lines[b0:b1])
    return ''.join(result)


def copy_new(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        raise ValueError('Refuse to overwrite an existing candidate file: ' + str(destination))
    shutil.copy2(source, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if not out.is_relative_to((ROOT / 'build').resolve()) or out.exists():
        parser.error('--out must be a new directory under sandcore/build')
    out.mkdir(parents=True)
    baseline = {name: sha(ROOT / name) for name in BASE}
    draft_inputs = {}
    for name in BASE:
        copy_new(ROOT / name, out / name)
    for suffix in DRAFTS:
        folder = ROOT / 'drafts' / ('m10a2-shell-' + suffix)
        for source in sorted(folder.rglob('*')):
            if not source.is_file() or '__pycache__' in source.parts:
                continue
            relative = source.relative_to(ROOT)
            draft_inputs[relative.as_posix()] = sha(source)
            copy_new(source, out / relative)
    passes = []
    for suffix, script, key in [('fs', 'make_integration_patch.py', 'desired'),
                                 ('host', 'make_patch.py', 'changes'),
                                 ('host', 'make_wm_patch.py', 'changes')]:
        folder = out / 'drafts' / ('m10a2-shell-' + suffix)
        result = runpy.run_path(str(folder / script))
        names = []
        for name, after in result[key].items():
            destination = out / name
            before = destination.read_bytes().decode('utf-8')
            destination.write_bytes(preserve_lines(before, after).encode('utf-8'))
            names.append(name)
        passes.append(dict(generator=script, changed_paths=sorted(names)))
        if script != 'make_wm_patch.py':
            for tree in ('kernel', 'user', 'tools'):
                for source in sorted((folder / tree).rglob('*')):
                    if source.is_file():
                        copy_new(source, out / source.relative_to(folder))
    library = out / 'drafts/m10a2-shell-r3'
    copy_new(library / 'SCSHELL.H', out / 'user/SCSHELL.H')
    for source in sorted(library.iterdir()):
        if source.is_file() and source.name != 'SCSHELL.H' and source.suffix.lower() in ('.c', '.h', '.json'):
            if source.name == 'source-manifest.json':
                continue
            copy_new(source, out / 'user/scshell' / source.name)
    for source in sorted((out / 'drafts/m10a2-shell-worker').iterdir()):
        if source.is_file() and (source.suffix.lower() in ('.c', '.h') or source.name == 'SCFILEWORK.json'):
            copy_new(source, out / 'user/shellworker' / source.name)
    for source in sorted((out / 'drafts/m10a2-shell-desktop').iterdir()):
        if source.is_file() and (source.suffix.lower() in ('.c', '.h', '.inc') or source.name == 'SCSHELL.json'):
            copy_new(source, out / 'user/scshell/desktop' / source.name)
    changed = {name: sha(ROOT / name) for name in BASE if sha(ROOT / name) != baseline[name]}
    if changed:
        raise RuntimeError('Active sources changed concurrently: ' + repr(changed))
    staged = {path.relative_to(out).as_posix(): sha(path) for tree in ('kernel', 'user', 'tools')
              for path in sorted((out / tree).rglob('*')) if path.is_file()}
    staged.update({name: sha(out / name) for name in ('Makefile', 'm10a2.mk')})
    report = dict(status='UNIFIED_REVIEW_CANDIDATE_NOT_BUILT_NOT_RUN',
                  active_sources_modified=False, full_M10a2=False,
                  baseline_sha256=baseline, draft_input_sha256=draft_inputs,
                  sequential_transformations=passes, candidate_sha256=staged)
    (out / 'integration-review.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(status=report['status'], paths=len(staged), out=str(out)), ensure_ascii=False))

if __name__ == '__main__':
    main()
