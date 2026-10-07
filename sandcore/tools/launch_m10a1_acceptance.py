#!/usr/bin/env python3
"""解压后有窗启动开发验收包；每次使用独立会话盘。"""
from datetime import datetime
from pathlib import Path
import secrets
import sys

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT/'tools'))
from scserial import main


if __name__ == '__main__':
    # 验收盘保持只读来源；用户编辑写在终端所显示的会话目录。
    # 下次传入--data旧会话盘即可继续，不把用户改动合并回基盘。
    directory = ROOT/'sessions'/('session-'+datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+secrets.token_hex(4))
    args = sys.argv[1:]
    readiness = [] if '--boot' in args else ['--core-symbols', str(ROOT/'images/core.sym')]
    sys.argv = [sys.argv[0], '--boot', str(ROOT/'images/sandcore.img'),
                '--data', str(ROOT/'images/sanddata.img'), '--out', str(directory),
                '--accel', 'auto', '--network', 'user', '--memory', '256']+readiness+args
    main()
