"""M9试玩入口：保留只读基线，每次从上次试玩盘创建新的会话副本。"""
import argparse
from datetime import datetime
import hashlib
import json
import msvcrt
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fresh', action='store_true', help='从随包基线重新开始；已有试玩记录保留')
    parser.add_argument('--headless', action='store_true', help='仅显示外部串口控制台，供自动化检查')
    parser.add_argument('--debug-only', action='store_true', help='独立COM1调试，不要求数据盘Shell可用')
    args = parser.parse_args()
    with (ROOT / 'play.lock').open('a+b') as lock:
        lock.seek(0)
        if lock.read(1) == b'':
            lock.write(b'0')
            lock.flush()
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            raise SystemExit('已有试玩正在运行，请先退出那个窗口。')
        manifest = json.loads((ROOT / 'manifest.json').read_text(encoding='utf-8'))
        for name, digest in manifest['baseline_sha256'].items():
            if hashlib.sha256((ROOT / 'images' / name).read_bytes()).hexdigest() != digest:
                raise SystemExit('随包基线校验失败：' + name)
        sessions = ROOT / 'sessions'
        sessions.mkdir(exist_ok=True)
        source = ROOT / 'images' / 'sanddata.img'
        candidates = sorted(sessions.glob('*/session.json'), reverse=True)
        if not args.fresh:
            for record in candidates:
                state = json.loads(record.read_text(encoding='utf-8'))
                disk = record.parent / 'sanddata.img'
                if state.get('status') == 'TRANSPORT_CONNECTED' and disk.is_file():
                    source = disk
                    break
        output = sessions / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
        print('M9 测试版；完整验收仍在进行。', flush=True)
        print('本次数据来源：' + str(source), flush=True)
        print('本次修改保存于：' + str(output / 'sanddata.img'), flush=True)
        print('正常退出请在串口控制台输入 :quit。', flush=True)
        command = [sys.executable, str(ROOT / 'tools' / 'scserial.py'),
                   '--boot', str(ROOT / 'images' / 'sandcore.img'),
                   '--data', str(source), '--out', str(output), '--accel', 'tcg']
        if args.headless:
            command.append('--headless')
        if args.debug_only:
            command.append('--debug-only')
        result = subprocess.call(command)
        msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
        return result


if __name__ == '__main__':
    raise SystemExit(main())
