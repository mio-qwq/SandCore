#!/usr/bin/env python3
"""M10a1独立有窗/外部串口入口，默认使用最新验收候选。"""
from datetime import datetime
from pathlib import Path
import secrets
import sys
from scserial import main

ROOT=Path(__file__).resolve().parents[1]
DEFAULT_BOOT=ROOT/'build/m10a1/sandcore.img'
DEFAULT_DATA=ROOT/'build/m10a1/sanddata.img'

if __name__=='__main__':
    # scserial先核副本摘要、连接UART及ACL、握手匿名QMP，再cont。
    # 不改M9默认/历史启动器，不把网络应用转发变为最高管理串口。
    directory=ROOT/'build/m10a1-sessions'/('session-'+datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+secrets.token_hex(4))
    args=sys.argv[1:]
    readiness=[] if '--boot' in args else ['--core-symbols',str(ROOT/'build/m10a1/core.sym')]
    # 旧work盘保留用户原内容；默认入口必须指向已修复隐藏故障卡的新核，
    # 不能让源码已更新而实际双击仍启动最早候选。显式--boot/--data仍可覆盖。
    sys.argv=[sys.argv[0],'--boot',str(DEFAULT_BOOT),
              '--data',str(DEFAULT_DATA),'--out',str(directory),
              '--accel','auto','--network','user','--memory','256']+readiness+args
    main()
