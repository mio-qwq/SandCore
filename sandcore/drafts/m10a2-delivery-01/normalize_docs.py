"""发布前规范化本批文本行尾；不改产品源或历史报告。"""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2];REPO=ROOT.parent
names=subprocess.check_output(['git','diff','--name-only'],cwd=REPO).decode().splitlines()
names+=['sandcore/docs/M10A2-ACCEPTANCE.md','sandcore/docs/M10A2-RELEASE.md']
for name in names:
    if not name.endswith('.md'):continue
    p=REPO/name;s=p.read_text(encoding='utf-8-sig')
    s=s.replace('build/m10a2-sdk-delivery-01','build/m10a2-sdk-delivery-02')
    if name=='README.md':
        s=s.replace('| [当前开发验收](sandcore/docs/M10A1-ACCEPTANCE.md) | M10a1 功能、运行方法、实际证据和限制 |','| [M10a2交付检查](sandcore/docs/M10A2-ACCEPTANCE.md) | 当前平台、SDK、真实证据与准确限制 |\n| [M10a2运行包/SDK](sandcore/docs/M10A2-RELEASE.md) | 交付内容与启动方法；实际发布以GitHub结果为准 |')
    if name=='README_EN.md':
        s=s.replace('| [Current Development Acceptance](sandcore/docs/M10A1-ACCEPTANCE.md)', '| [M10a2 Delivery Audit](sandcore/docs/M10A2-ACCEPTANCE.md)')
    p.write_bytes(s.encode('utf-8'))
p=ROOT/'docs/BUILD.md';s=p.read_text(encoding='utf-8-sig');title,rest=s.split('\n',1)
note='''> **2026-10-17 M10a2交付构建入口：** 统一宿主构建为 `python3 tools/build_m10a2_p6.py --tag p6-unified-host-21 --build build/m10a2-main-session-return-01`（WSL、sandcore工作目录）；报告HOST21已实际exit0。真实客体八库/G1/SCLD及应用生成来源由native11严格清单记录；最后CORE为final12。最终公开SDK按同一来源及最新规范restage到build/m10a2-sdk-delivery-02。四文件运行包直接run.bat，不要求重建。精确执行器、来源摘要和发布审计见drafts/m10a2-p6-final-12-receipt、drafts/m10a2-delivery-01及M10A2-ACCEPTANCE.md。旧默认make/build.bat与历史版本入口仍保留，不把它们等同于本次已绑定的原生产物。下方各日期记录为历史。'''
p.write_bytes((title+'\n\n'+note+'\n'+rest+'\n修订：2026-10-17，补M10a2真实构建/原生来源及最终运行包入口。\n').encode('utf-8'))
print('CURRENT_DOC_LINE_ENDINGS_AND_ENTRY_LINKS_NORMALIZED')
