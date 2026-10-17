from pathlib import Path
import json

ROOT=Path(__file__).resolve().parents[2]
for name in ('STUDIO2.JSON','STUDIO2.NATIVE.JSON'):
    p=ROOT/'user/studio2'/name
    manifest=json.loads(p.read_bytes())
    names={row['name'] for row in manifest['imports']}
    for symbol in ('qsort','fread'):
        if symbol not in names:
            manifest['imports'].append(dict(name=symbol,dependency='SCCLIB.SCL',kind='function',size=1))
    manifest['imports'].sort(key=lambda row:row['name'])
    p.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')

p=ROOT/'docs/SCW-WORKSPACE.md'
s=p.read_text(encoding='utf-8')
s=s.replace('“新建工作区”在内存建立默认配置。第一次保存由用户选择目录和名称；无扩展名时使用 .SCW。',
    '“新建项目”先选择保存文件夹，再填写名称和类型；程序自动建立命名文件夹、MAIN.C、BUILD 及 .SCW 项目。项目名称无需后缀。另存项目时无扩展名使用 .SCW。')
s=s.replace('F5构建、F6运行、F7调试','F7构建、Ctrl+F5运行、F5调试')
s=s.replace('project 节保留 kind','普通应用可使用 auto_link=true 自动链接系统库，automatic=1u 为单文件模式，2u 为自动多文件项目；缺少这些字段的旧 SCW 仍按显式清单处理。库目标和第三方依赖保留手动清单。\n\nproject 节保留 kind')
s+='\n修订：2026-10-17，新建项目向导、自动链接字段和快捷键；单文件无需先保存 SCW。详见 STUDIO2-USER-GUIDE。\n'
p.write_text(s,encoding='utf-8')
p=ROOT/'docs/M10A2-SDK.md'
s=p.read_text(encoding='utf-8').replace('SCUI为ABI1.4/v6','SCUI为ABI1.4/v7')
s+='\n修订：2026-10-17，恢复三个旧公开兼容头的 INC 接线，新增 Studio 用户说明；本轮运行结果另列。\n'
p.write_text(s,encoding='utf-8')
print('SOURCE_MANIFESTS_AND_DOCS_UPDATED')
