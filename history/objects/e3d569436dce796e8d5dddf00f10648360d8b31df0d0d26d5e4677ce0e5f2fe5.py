#!/usr/bin/env python3
"""mio：开IRQ整帧复制后的连续回归，不覆盖之前阶段的成功证据。

每套验证独立解释器、独立双盘QEMU、独立输出子目录。模块导入会
设置旧OUT全局，不能靠一次import后改一个变量让四套脚本混存结果；
全部相关模块明确指向本套目录。串行运行，失败立即停，子脚本
finally只退出自己的QEMU。构建输入锁定，不能边测试边重建。
"""
import hashlib,json,subprocess,sys
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/m8-frame-irq'

def main():
    inputs={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in
            ('build/sandcore.img','build/kernel.elf','build/kernel.sym','user/SCAPI.H','user/NUI.inc','user/settings.c')}
    cases=(('rgb-regression','verify_truecolor'),('cpu-regression','verify_cpu_storage'),
           ('settings-regression','verify_settings'),('userspace-regression','verify_userspace'))
    for tag,module in cases:
        target=OUT/tag;target.mkdir(parents=True,exist_ok=True)
        # 主函数中仍可设置编译器OUT；相关对象使用同一目录，宿主
        # 编译协议只生成真实键盘命令，不注入CPU函数或修改客体页表。
        code=f'''import sys
sys.path.insert(0,"tools")
from pathlib import Path
import {module} as m
p=Path("build/m8-frame-irq/{tag}");p.mkdir(parents=True,exist_ok=True)
m.OUT=p
if hasattr(m,"t"):m.t.OUT=m.t.q.OUT=p
if hasattr(m,"q"):m.q.OUT=p
if hasattr(m,"compiler"):m.compiler.OUT=p
if hasattr(m,"theme"):m.theme.OUT=p
m.main()
'''
        print('开始 '+tag,flush=True)
        with (target/'run.log').open('w',encoding='utf-8') as log:
            subprocess.run([sys.executable,'-c',code],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
        assert inputs=={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in inputs},'回归期间输入发生变化'
        result=json.loads((target/'results.json').read_text(encoding='utf-8'))
        if isinstance(result,list):assert len(result)==9
        else:assert result['status']=='PASS'
        print('通过 '+tag,flush=True)
    report=dict(author='mio',status='PASS',inputs_sha256=inputs,cases=[tag for tag,module in cases],
                limits='RGB三轮/VGA/32MB；CPU新512旧192盘；Settings与19CLI既有交互矩阵。不是全M8五档×三档/全部工具/游戏/光追完成。')
    (OUT/'regressions.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False),flush=True)

if __name__=='__main__':main()
