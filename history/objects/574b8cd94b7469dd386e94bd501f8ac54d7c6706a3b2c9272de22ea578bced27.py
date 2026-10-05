#!/usr/bin/env python3
"""mio：同一真正G2调试器1080放大失败的独立只读复现。

只改变证据输出目录并增加失败观察记录，原鼠标路径、全部
布局/现场/像素比较和资源检查完全使用正式矩阵规则。结果
仅属于这组复现，不能替代重新执行十九组合的整体矩阵。
"""
import hashlib,json
from pathlib import Path
import verify_debugger_matrix as matrix


def main():
    stage=matrix.STAGE;matrix.OUT=stage/'enlarge-observer'
    assert not matrix.OUT.exists(),'独立观察不得覆盖原资料'
    core=json.loads((stage/'results.json').read_text(encoding='utf-8'))
    assert core['status']=='PASS'
    inputs=dict(core['inputs_sha256'])
    for name in ('build/fs/SYS/THEMES/AURORA.CFG','build/fs/SYS/THEMES/CLASSIC.CFG'):
        inputs[name]=matrix.sha(matrix.ROOT/name)
    assert inputs=={name:matrix.sha(matrix.ROOT/name) for name in inputs}
    native=(stage/'debugger-native.scx').read_bytes();mapping=(stage/'debugger-native.map').read_bytes()
    target=(stage/'target-native.scx').read_bytes()
    assert hashlib.sha256(native).hexdigest()==core['native']['debugger']['scx_sha256']
    assert hashlib.sha256(mapping).hexdigest()==core['native']['debugger']['map_sha256']
    report=matrix.run_case(1920,1080,100,'AURORA','std',native,mapping,target,inputs)
    print(json.dumps(dict(author='mio',status='PASS',case=report,
        limits='同一真正G2产物1080极小放大独立复现，只读取证，不代替十九组合'),ensure_ascii=False),flush=True)


if __name__=='__main__':main()
