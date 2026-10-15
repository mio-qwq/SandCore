#!/usr/bin/env python3
"""复制本批真实证据并逐份核SHA；不启动客体、不复制磁盘、不修改原报告。"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def digest(body):
    return hashlib.sha256(body).hexdigest()


def inside(path, directory):
    value = path.resolve(strict=True)
    if not value.is_relative_to(directory.resolve()):
        raise ValueError('证据路径超出指定来源目录：' + str(value))
    return value


class Archive:
    def __init__(self, out):
        self.out = out.resolve()
        if not self.out.is_relative_to((ROOT / 'evidence').resolve()):
            raise ValueError('归档只能写入本仓库evidence目录')
        self.out.mkdir(parents=True, exist_ok=True)
        self.files = []
        self.by_hash = {}

    def add(self, source, name, role, expected=None, size=None):
        source = inside(Path(source), ROOT)
        if source.suffix.lower() in ('.img', '.qcow', '.qcow2', '.vhd', '.vhdx'):
            raise ValueError('归档不复制磁盘：' + str(source))
        body = source.read_bytes()
        value = digest(body)
        if (expected is not None and expected != value) or (size is not None and size != len(body)):
            raise ValueError('来源实际字节与记录不同：' + str(source))
        if value in self.by_hash:
            target = self.by_hash[value]
        else:
            target = self.out / name
            if not target.resolve().is_relative_to(self.out):
                raise ValueError('非法归档相对路径')
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists() and target.read_bytes() != body:
                raise ValueError('拒绝覆盖不同的旧证据：' + str(target))
            if not target.exists():
                target.write_bytes(body)
            if digest(target.read_bytes()) != value:
                raise ValueError('复制后SHA不同：' + str(target))
            self.by_hash[value] = target
        item = dict(source=str(source), archive=target.relative_to(self.out).as_posix(),
            role=role, bytes=len(body), sha256=value)
        self.files.append(item)
        return item

    def stage(self, path, label, screenshots):
        path = inside(path, ROOT / 'build')
        report = json.loads(path.read_text(encoding='utf-8'))
        self.add(path, 'reports/' + label + '.json', label + ':RAW_REPORT')
        if report.get('source_disk_unchanged') is not True or report.get('current_inputs_unchanged') is not True:
            raise ValueError('终态报告没有来源和原盘守恒：' + label)
        if report.get('status') in ('PREFLIGHT', 'RUNNING'):
            raise ValueError('仍运行的报告不可归档：' + label)
        for case in report.get('cases', []):
            if not case.get('stdout_path'):
                continue
            source = inside(Path(case['stdout_path']), ROOT / 'build')
            safe = re.sub('[^A-Za-z0-9_.-]', '_', case['name'])
            self.add(source, 'stdout/' + label + '/' + safe + '.bin',
                label + ':CASE_STDOUT:' + case['name'], case['stdout_sha256'], case['stdout_bytes'])
        for folder in ('native-artifacts', 'native-libraries'):
            provenance = path.parent / folder / 'provenance.json'
            if provenance.is_file():
                self.add(provenance, 'provenance/' + label + '-' + folder + '.json',
                    label + ':RAW_GENERATION_PROVENANCE')
        for source in sorted(path.parent.glob('file-*-log')) + sorted(path.parent.glob('file-*-map')):
            self.add(source, 'compile/' + label + '/' + source.name, label + ':RAW_COMPILE_LOG_MAP')
        for name in ('console.txt', 'debug-rx.bin', 'qemu.log', 'session.json'):
            source = path.parent / name
            if source.is_file():
                self.add(source, 'sessions/' + label + '/' + name, label + ':RAW_SESSION')
        if screenshots:
            for item in report.get('screenshots', []):
                source = inside(Path(item['path']), path.parent)
                self.add(source, 'screenshots/' + label + '/' + source.name,
                    label + ':ACTUAL_SCREENSHOT', item['sha256'])
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--first-report', type=Path, default=ROOT / 'build/m10a2-ui-platform-guest-05/ui-platform.json')
    parser.add_argument('--refresh-report', type=Path, default=ROOT / 'build/m10a2-ui-native-refresh-02/ui-platform.json')
    parser.add_argument('--text-report', type=Path, default=ROOT / 'build/m10a2-ui-gui-guest-09/ui-gui.json')
    parser.add_argument('--gui-report', type=Path, required=True)
    args = parser.parse_args()
    archive = Archive(args.out)
    reports = {}
    for label, path, pictures in (('first05', args.first_report, False),
                                ('publication02', args.refresh_report, True),
                                ('text09-GUI-FAIL', args.text_report, True),
                                (args.gui_report.parent.name, args.gui_report, True)):
        reports[label] = archive.stage(path, label, pictures)
    first = reports['first05']
    refresh = reports['publication02']
    text = reports['text09-GUI-FAIL']
    unloaded = text['native_checks_reuse']
    origin = Path(unloaded['source_report'])
    if digest(origin.read_bytes()) != unloaded['source_report_sha256']:
        raise ValueError('07旧平台与DEFAULT实际来源报告改变')
    reports['native07-GUI-FAIL'] = archive.stage(origin, 'native07-GUI-FAIL', False)
    for expected, path in ((
            '5535d2fdb7c6736aa7321d250bf521564055253b319199b6bf85d7f594c7e993', args.first_report), (
            '74dc0f522c2c57e77a6be30d613a11f81d8d93d1d6fa67efa4b851ac1850fe43', args.refresh_report), (
            '449756cee63a9ae451f30933e5a0c3d64ace049465a77fac4929a698d6c19be8', args.text_report)):
        if digest(path.read_bytes()) != expected:
            raise ValueError('固定首轮/刷新/文本原报告改变：' + str(path))
    for name, item in first['native_artifacts'].items():
        archive.add(item['path'], 'native/' + name, 'FIRST05_ACTUAL_G1_GENERATED:' + name,
            item['sha256'], item['bytes'])
    for name, item in first['native_libraries'].items():
        location = 'native/origin-first05/' + name if name == 'SCUI.SCL' else 'native/' + name
        archive.add(item['path'], location, 'FIRST05_ACTUAL_G1_GENERATED:' + name,
            item['sha256'], item['bytes'])
    item = refresh['native_libraries']['SCUI.SCL']
    archive.add(item['path'], 'native/publication02/SCUI.SCL', 'PUBLICATION02_FRESH_GENERATED_NOT_EXECUTED_IN_REFRESH',
        item['sha256'], item['bytes'])
    # 宿主实现随后会推进新模式，归档当前版本以免旧SHA只剩文本声明。
    for name in ('tools/refresh_m10a2_ui_native.py', 'docs/UI-NATIVE-REFRESH.md',
                 'tools/verify_m10a2_ui_gui.py', 'tools/scui_input.py', 'tools/scui_workflow.py',
                 'tools/archive_m10a2_ui_proof.py'):
        archive.add(ROOT / name, 'host/' + name, 'ARCHIVE_TIME_HOST_SOURCE_NOT_CLAIMED_AS_ALL_PRIOR_INPUTS')
    source_hashes = {label: report.get('input_sha256', {}) for label, report in reports.items()}
    final = reports[args.gui_report.parent.name]
    value = dict(schema=1, scope='M10A2_UI_DEFAULT_PUBLICATION_COMPACT_PROOF_DRAFT',
        full_M10a2=False, GUI_behavior_verified=final.get('GUI_behavior_verified') is True,
        final_GUI_status=final['status'], report_statuses={name: r['status'] for name, r in reports.items()},
        source_hashes=source_hashes, files=archive.files,
        logical_file_references=len(archive.files), unique_copied_files=len(archive.by_hash),
        unique_copied_bytes=sum(path.stat().st_size for path in archive.by_hash.values()),
        no_disks_copied=True, no_original_reports_modified=True,
        distinction='FIRST05_ORIGINAL_GENERATION__PUBLICATION02_FRESH_SCUI__TEXT09_ACTUAL_PASS_GUI_FAIL__FINAL_GUI_ACTUAL_STATUS')
    name = 'archive-' + args.gui_report.parent.name + '.json'
    path = archive.out / name
    body = (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    if path.exists() and path.read_bytes() != body:
        raise ValueError('拒绝重写同名归档索引：' + str(path))
    if not path.exists():
        path.write_bytes(body)
    print(json.dumps(dict(index=str(path), index_sha256=digest(body),
        references=value['logical_file_references'], files=value['unique_copied_files'],
        bytes=value['unique_copied_bytes'], status=final['status'],
        GUI_behavior_verified=value['GUI_behavior_verified']), ensure_ascii=False))


if __name__ == '__main__':
    main()
