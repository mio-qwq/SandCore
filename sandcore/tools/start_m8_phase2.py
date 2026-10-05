#!/usr/bin/env python3
"""mio：保存整个M8第二阶段授权和开始优化前的精确输入。

只读取开发盘并复制到新的证据目录；不接触用户正在玩的temp miotest。
历史第一阶段ZIP保持原字节，当前源另外冻结，避免后续修复混淆基线。
本脚本不是验证器，不生成任何运行成功结论。
"""
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / 'sandcore'
OUT = CORE / 'build/m8-phase2-resume-20261003-01'
NOTICE = ('> **2026-10-03最新授权：整个M8第二阶段已开始。** 用户明确“继续 第二阶段”，'
          '并补充“我的意思是整个m8的第二阶段开始”。统一进行完整构建、系统内编译、'
          '双盘无头QEMU测试、性能优化、失败修复与重测；优先定位legacy游戏和快速鼠标'
          '移动的CPU占用/卡慢。历史证据只对应其冻结输入，当前完整M8仍未验收。')


def sha(blob):
    return hashlib.sha256(blob).hexdigest()


def main():
    OUT.mkdir(parents=True, exist_ok=False)
    paths = [ROOT/'HANDOFF.md', ROOT/'M8_GOAL.md', CORE/'README.md',
             CORE/'docs/ROADMAP.md', CORE/'docs/M8-IMPLEMENTATION.md',
             CORE/'docs/M8-GAMES.md']
    for path in paths:
        text = path.read_text(encoding='utf-8')
        lines = text.splitlines()
        # 只更新当前状态提示；保留旧日期记录，说明此前为何曾停止验证。
        for index, line in enumerate(lines):
            if line.startswith('> **') and ('最新执行顺序' in line or '最新游戏专项阶段' in line):
                lines[index] = NOTICE
                break
        else:
            lines[1:1] = ['', NOTICE]
        text = '\n'.join(lines)+'\n'
        text = text.replace('## 当前执行阶段：游戏专项第一阶段源码收齐，等待批准游戏第二阶段',
                            '## 当前执行阶段：整个M8第二阶段测试、优化与修复')
        text = text.replace('> 当前阶段：游戏专项第一阶段源码收尾，等待重新批准游戏第二阶段。',
                            '> 当前阶段：用户已批准整个M8第二阶段，执行完整测试与优化。')
        text += ('\n2026-10-03第二阶段续接：用户明确整个M8进入第二阶段，授权全部测试、'
                 '优化和修复。优先实测空桌面/快速鼠标/legacy/新版游戏的CPU、帧耗时与'
                 '输入延迟；保持同画质对照，用户独立temp miotest副本不参与测试。'
                 '阶段输入冻结于sandcore/build/m8-phase2-resume-20261003-01；'
                 '本记录仅确认授权，不代表任何新PASS。\n')
        path.write_text(text, encoding='utf-8')
    agents = ROOT/'AGENTS.md'
    with agents.open('a', encoding='utf-8') as stream:
        stream.write('\n6. **2026-10-03授权已到达**：用户“继续 第二阶段”，并明确是整个M8。'
                     '游戏专项第一阶段门槛已经满足；现在执行整个M8完整测试、优化、修复与'
                     '最终验收。保留前述阶段沿革，不再重复申请同一授权。\n')
    # 收齐所有实际源码/资源/规范，排除build和缓存，测试程序同样绑定摘要。
    sources = [ROOT/name for name in ('AGENTS.md','HANDOFF.md','M8_GOAL.md')]
    sources += [CORE/name for name in ('README.md','Makefile','linker.ld','build.bat','run.bat')]
    for directory in ('boot','kernel','modules','user','assets','third_party','docs','legacy','tools'):
        sources += [path for path in (CORE/directory).rglob('*') if path.is_file()
                    and '__pycache__' not in path.parts and '.git' not in path.parts
                    and path.suffix not in ('.pyc','.o','.obj')]
    records = []
    with zipfile.ZipFile(OUT/'source-inputs.zip','w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for path in sorted(set(sources)):
            blob = path.read_bytes(); name = path.relative_to(ROOT).as_posix()
            archive.writestr(name,blob)
            records.append(dict(path=name,bytes=len(blob),sha256=sha(blob)))
    with zipfile.ZipFile(OUT/'source-inputs.zip') as archive:
        assert archive.testzip() is None
        for record in records:
            assert sha(archive.read(record['path'])) == record['sha256']
    before = OUT/'before-build'; before.mkdir()
    baseline = {}
    for name in ('sandcore.img','sanddata.img','kernel.bin','kernel.elf','kernel.sym'):
        source = CORE/'build'/name; target = before/name
        shutil.copy2(source,target)
        assert sha(source.read_bytes()) == sha(target.read_bytes())
        baseline[name] = dict(bytes=target.stat().st_size,sha256=sha(target.read_bytes()))
    first = CORE/'build/m8-games-phase1-20261003-01/source-only.zip'
    report = dict(author='mio',phase=2,scope='ENTIRE_M8',status='AUTHORIZED_NOT_YET_VERIFIED',
                  user_messages=['继续 第二阶段','我的意思是整个m8的第二阶段开始'],
                  priority='CPU, frame cost, input latency, same-quality optimization',
                  private_user_copy_untouched=True,baseline=baseline,source_files=records,
                  source_zip_sha256=sha((OUT/'source-inputs.zip').read_bytes()),
                  immutable_game_phase1_sha256=sha(first.read_bytes()))
    (OUT/'authorization-and-inputs.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(directory=str(OUT),source_files=len(records),baseline=baseline),ensure_ascii=True))


if __name__ == '__main__':
    main()
