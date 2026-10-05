#!/usr/bin/env python3
"""mio：整片队列的真实进程身份/失败保留/帧清单边界验证。

只产生私有宿主夹具，不启动另一份耗时的正式Blender。真实4K格式
PNG用于校验提交合同；平色夹具仅用于边界，不冒充影片画质或帧率。
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import traceback
import zlib

import film_queue as queue


def chunk(kind, body):
    return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True)
    args = parser.parse_args(); out = Path(args.out).resolve(); out.mkdir(parents=True, exist_ok=False)
    sources = out/'source'; sources.mkdir()
    for name, source in (('verifier.py', Path(__file__)), ('film_queue.py', Path(queue.__file__)),
                         ('film_io.py', Path(queue.__file__).with_name('film_io.py'))):
        (sources/name).write_bytes(source.read_bytes())
    report = dict(author='mio', scope='HOST_QUEUE_BOUNDARIES_ONLY', status='RUNNING', cases={},
                  source_sha256={str(Path('source')/name):queue.digest(sources/name)
                                 for name in ('verifier.py','film_queue.py','film_io.py')})
    try:
        data = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 3840, 2160, 16, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(bytes((1+3840*6)*2160))) + chunk(b'IEND', b''))
        frame = out / 'frame-02221.png'; frame.write_bytes(data)
        identity = hashlib.sha256(b'private-queue-fixture-mio').hexdigest()
        good = dict(frame=2221, seconds=1, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        def setup(rows, sha=identity):
            queue.commit(out / 'progress.json', dict(identity_sha256=sha, frames=rows))
        setup([good]); verified = {}
        assert queue.completed_frames(out, identity, verified) == [good]
        assert queue.completed_frames(out, identity, verified) == [good]
        report['cases']['4k16-complete-cached'] = 'PASS'
        for name, rows, sha in (
            ('duplicate-frame', [good, good], identity),
            ('outside-frame', [{**good, 'frame':2521}], identity),
            ('wrong-identity', [good], 'bad'),
            ('wrong-size', [{**good, 'bytes':len(data)+1}], identity),
            ('wrong-sha', [{**good, 'sha256':'bad'}], identity),
        ):
            setup(rows, sha)
            try:
                queue.completed_frames(out, identity, {})
            except (ValueError, AssertionError):
                report['cases'][name] = 'REJECT'
            else:
                raise AssertionError('应拒绝：' + name)
        setup([good]); frame.write_bytes(data[:-1])
        try:
            queue.completed_frames(out, identity, verified)
        except ValueError:
            report['cases']['cache-invalidated-after-change'] = 'REJECT'
        else:
            raise AssertionError('缓存掩盖了损坏图像')
        frame.write_bytes(data); setup([good])
        queue.completed_frames(out, identity, verified)
        assert frame.read_bytes() == data
        report['cases']['good-frame-retained-after-failures'] = 'PASS'
        worker = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(1)'],
                                  creationflags=subprocess.CREATE_NO_WINDOW)
        observer = queue.ProcessObservation(worker.pid, sys.executable)
        try:
            assert observer.alive()
            created = observer.created
            for name, exe, when in (('pid-creation-mismatch', sys.executable, created+1),
                                    ('executable-mismatch', out/'not-python.exe', created)):
                try:
                    bad = queue.ProcessObservation(worker.pid, exe, when)
                except ValueError:
                    report['cases'][name] = 'REJECT'
                else:
                    bad.close(); raise AssertionError('应拒绝：' + name)
            worker.wait(timeout=5)
            assert not observer.alive() and observer.exit_code() == 0
            report['cases']['actual-process-alive-exit'] = 'PASS'
        finally:
            observer.close()
            worker.wait(timeout=5)
        report.update(frame_sha256=good['sha256'],
                      limitations='仅队列/真实Windows进程与4K PNG边界，不是整片/编码/客体播放器验收')
        # 源码与规格是续渲身份的一部分。只修改配置文件的分辨率或
        # 一个字节的源码，即使旧帧还在，也必须拒绝继续同批次渲染。
        (out/'film_scene.py').write_bytes(b'# private source / mio\n')
        (out/'film_io.py').write_bytes(b'# private validation / mio\n')
        frozen = dict(magic='SCFILM1MIO', mode='master', width=3840, height=2160,
                      output_bits=16, fps=60, seconds=42, samples=512,
                      source_sha256=queue.digest(out/'film_scene.py'),
                      io_source_sha256=queue.digest(out/'film_io.py'))
        queue.commit(out/'project.json', frozen); frozen_sha = queue.digest(out/'project.json')
        assert queue.check_identity(out, frozen_sha) == frozen
        report['cases']['frozen-identity-valid'] = 'PASS'
        (out/'film_scene.py').write_bytes(b'# changed source / mio\n')
        try:
            queue.check_identity(out, frozen_sha)
        except ValueError:
            report['cases']['source-change-refused'] = 'REJECT'
        else:
            raise AssertionError('修改源码后仍接纳')
        (out/'film_scene.py').write_bytes(b'# private source / mio\n')
        queue.commit(out/'project.json', {**frozen, 'width':1920})
        for name, sha in (('manifest-change-refused', frozen_sha),
                          ('quality-change-refused', queue.digest(out/'project.json'))):
            try:
                queue.check_identity(out, sha)
            except ValueError:
                report['cases'][name] = 'REJECT'
            else:
                raise AssertionError('应拒绝：' + name)
        queue.commit(out/'project.json', frozen)
        # 真正持有Windows字节锁后启动第二个独立调度器，验证其只
        # 留自身失败记录、不会覆盖已有活动状态，更不会开始Blender。
        import msvcrt
        queue.commit(out/'queue-plan.json', {})
        active = dict(status='PRIVATE_ACTIVE_FIXTURE', marker='MIO')
        queue.commit(out/'queue-status.json', active)
        with (out/'queue.lock').open('w+b') as lock:
            lock.write(b'\0'); lock.flush(); lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            duplicate = subprocess.run([sys.executable, str(Path(queue.__file__).resolve()), '--batch', str(out)],
                                       capture_output=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
        assert duplicate.returncode != 0
        assert json.loads((out/'queue-status.json').read_text(encoding='utf-8')) == active
        failures = list(out.glob('queue-failure-*.json')); assert len(failures) == 1
        failure = json.loads(failures[0].read_text(encoding='utf-8'))
        assert failure['status'] == 'FAILED_RETAINED_OUTPUT' and 'locking' in failure['traceback']
        report['cases']['duplicate-queue-lock-retains-active-state'] = 'REJECT'
        report['status'] = 'PASS'
    except Exception as error:
        report.update(status='FAIL', error=repr(error), traceback=traceback.format_exc())
        raise
    finally:
        queue.commit(out/'results.json', report)
        print(json.dumps(report, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    main()
