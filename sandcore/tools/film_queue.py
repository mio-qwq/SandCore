#!/usr/bin/env python3
"""mio：冻结工程的整片后台调度、真实进度与断点续渲。

调度器与耗时的Blender进程分离：可以接管已启动的试帧，等待真实
进程退出并完整校验输出，再执行all。文件中有PID不等于任务存活，
也不等于已有成片；Windows进程句柄和创建时间用于防止PID复用。
不结束其它进程，不插帧、不降低正式规格、不覆盖冻结工程源码。
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import traceback

from film_io import validate_png


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def commit(path, value):
    # 同目录原子替换：观察者不会读到只有半个对象的进度JSON。
    pending = path.with_name(path.name + '.pending')
    pending.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    pending.replace(path)


def check_identity(batch, expected):
    manifest = batch / 'project.json'
    if digest(manifest) != expected:
        raise ValueError('冻结工程身份改变；不能混入另一工程的帧')
    identity = json.loads(manifest.read_text(encoding='utf-8-sig'))
    for filename, key in (('film_scene.py', 'source_sha256'), ('film_io.py', 'io_source_sha256')):
        if digest(batch / filename) != identity[key]:
            raise ValueError('冻结源码改变：' + filename)
    if (identity['magic'], identity['mode'], identity['width'], identity['height'],
        identity['output_bits'], identity['fps'], identity['seconds'], identity['samples']) != (
        'SCFILM1MIO', 'master', 3840, 2160, 16, 60, 42, 512):
        raise ValueError('此队列只接受约定的4K/60FPS/42秒/512采样正式工程')
    return identity


def completed_frames(batch, identity_sha, verified):
    """只认可记录摘要与完整PNG一致的帧；失败/临时文件不计进度。

    每帧首次出现时核全部块CRC、DEFLATE及逐行格式；后续保留
    文件大小/修改时间的缓存，文件变化立即重验，最终交付另全量复核。
    progress的requested_frames可能仍是试帧的1，不能据此误报整片100%。
    """
    progress_path = batch / 'progress.json'
    if not progress_path.exists():
        return []
    # 冻结旧渲染器的最后一次状态写入不是原子操作。仅短暂重读JSON，
    # 不把持续损坏吞掉；数据/摘要/图像错误始终立即中止并留失败证据。
    for attempt in range(3):
        try:
            progress = json.loads(progress_path.read_text(encoding='utf-8-sig'))
            break
        except json.JSONDecodeError:
            if attempt == 2:
                raise
            time.sleep(.2)
    if progress['identity_sha256'] != identity_sha:
        raise ValueError('帧清单属于另一工程')
    rows = progress['frames']
    unique = set()
    for row in rows:
        frame = row['frame']
        if type(frame) is not int or not 1 <= frame <= 2520 or frame in unique:
            raise ValueError('帧号无效/重复')
        unique.add(frame)
        path = batch / ('frame-%05d.png' % frame)
        stat = path.stat()
        signature = (stat.st_size, stat.st_mtime_ns, row['sha256'])
        if stat.st_size != row['bytes']:
            raise ValueError('帧大小与清单不符：' + str(frame))
        if verified.get(frame) != signature:
            if digest(path) != row['sha256']:
                raise ValueError('帧摘要不符：' + str(frame))
            validate_png(path, 3840, 2160, 16)
            verified[frame] = signature
    return rows


class ProcessObservation:
    """只读Windows进程句柄；不凭PID文本判断活着，不请求终止权限。"""
    def __init__(self, pid, expected_executable, expected_created=None):
        self.api = C.WinDLL('kernel32', use_last_error=True)
        self.api.OpenProcess.argtypes = (W.DWORD, W.BOOL, W.DWORD)
        self.api.OpenProcess.restype = W.HANDLE
        self.api.GetProcessTimes.argtypes = (W.HANDLE, C.POINTER(W.FILETIME), C.POINTER(W.FILETIME),
                                           C.POINTER(W.FILETIME), C.POINTER(W.FILETIME))
        self.api.QueryFullProcessImageNameW.argtypes = (W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD))
        self.api.WaitForSingleObject.argtypes = (W.HANDLE, W.DWORD)
        self.api.WaitForSingleObject.restype = W.DWORD
        self.api.GetExitCodeProcess.argtypes = (W.HANDLE, C.POINTER(W.DWORD))
        self.api.CloseHandle.argtypes = (W.HANDLE,)
        self.handle = self.api.OpenProcess(0x1000 | 0x100000, False, pid)
        if not self.handle:
            raise C.WinError(C.get_last_error())
        try:
            names = C.create_unicode_buffer(32768); length = W.DWORD(len(names))
            if not self.api.QueryFullProcessImageNameW(self.handle, 0, names, C.byref(length)):
                raise C.WinError(C.get_last_error())
            if os.path.normcase(names.value) != os.path.normcase(str(Path(expected_executable).resolve())):
                raise ValueError('PID不是约定的Blender可执行文件')
            created = W.FILETIME(); ended = W.FILETIME(); kernel = W.FILETIME(); user = W.FILETIME()
            if not self.api.GetProcessTimes(self.handle, C.byref(created), C.byref(ended),
                                           C.byref(kernel), C.byref(user)):
                raise C.WinError(C.get_last_error())
            self.created = created.dwLowDateTime | (created.dwHighDateTime << 32)
            if expected_created is not None and self.created != expected_created:
                raise ValueError('PID已被复用，不接管新进程')
        except Exception:
            self.close()
            raise

    def alive(self):
        status = self.api.WaitForSingleObject(self.handle, 0)
        if status == 258:
            return True
        if status != 0:
            raise C.WinError(C.get_last_error())
        return False

    def exit_code(self):
        result = W.DWORD()
        if not self.api.GetExitCodeProcess(self.handle, C.byref(result)):
            raise C.WinError(C.get_last_error())
        return result.value

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--batch', required=True)
    args = parser.parse_args()
    batch = Path(args.batch).resolve(); plan_path = batch / 'queue-plan.json'
    plan = json.loads(plan_path.read_text(encoding='utf-8-sig'))
    state_path = batch / 'queue-status.json'
    state = dict(author='mio', magic='SCFQUEUE1MIO', status='STARTING', queue_pid=os.getpid(),
                 batch=str(batch), target_frames=2520, target_resolution=[3840, 2160],
                 fps=60, seconds=42, sequence_complete=False, encoded_movie_complete=False,
                 guest_player_complete=False, started_local=time.strftime('%Y-%m-%dT%H:%M:%S'))
    observer = None; producer = None; verified = {}; lock = None; owns_lock = False; active_pid = None
    try:
        import msvcrt
        # 操作系统字节锁在异常退出时自动释放；陈旧PID/锁文件不会永久
        # 阻止续渲。同批只能一个调度器，不依赖不原子的“先看文件再创建”。
        lock = (batch / 'queue.lock').open('a+b')
        lock.seek(0, os.SEEK_END)
        if lock.tell() == 0:
            lock.write(b'\0'); lock.flush()
        lock.seek(0); msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        owns_lock = True
        identity = check_identity(batch, plan['identity_sha256'])
        frozen_queue = batch / 'film_queue.py'
        if digest(Path(__file__).resolve()) != plan['queue_source_sha256'] or digest(frozen_queue) != plan['queue_source_sha256']:
            raise ValueError('冻结调度源码改变')
        adoption = plan.get('adopt_process')
        def update(stage):
            rows = completed_frames(batch, plan['identity_sha256'], verified)
            sizes = [row['bytes'] for row in rows]; timings = [row['seconds'] for row in rows]
            state.update(status=stage, completed_verified_frames=len(rows), remaining_frames=2520-len(rows),
                         completed_bytes=sum(sizes), disk_free_bytes=shutil.disk_usage(batch).free,
                         observed_local=time.strftime('%Y-%m-%dT%H:%M:%S'),
                         active_blender_pid=active_pid)
            if timings:
                state['mean_completed_frame_seconds'] = round(sum(timings)/len(timings), 3)
                state['projected_sequence_bytes'] = round(sum(sizes)/len(sizes)*2520)
                state['estimate_limitations'] = '仅已完成帧均值；不同镜头成本变化，不是承诺的完工时间'
            commit(state_path, state)
            return rows
        if adoption:
            existing_rows = completed_frames(batch, plan['identity_sha256'], verified)
            try:
                observer = ProcessObservation(adoption['pid'], plan['blender'], adoption['created_filetime'])
            except OSError as error:
                if error.winerror not in (87, 1168) or adoption['frame'] not in {row['frame'] for row in existing_rows}:
                    raise
            if observer:
                active_pid = adoption['pid']
                update('WAITING_EXISTING_4K_TRIAL')
                while observer.alive():
                    time.sleep(10); update('WAITING_EXISTING_4K_TRIAL')
                state['adopted_exit_code'] = observer.exit_code(); observer.close(); observer = None
                active_pid = None
            else:
                # 断点续渲可能发生在试帧进程早已退出之后；已完整校验的
                # 试帧是有效交接，不要求历史PID现在仍存活。
                state['adopted_exit_code'] = 0
            rows = update('CHECKING_COMPLETED_TRIAL')
            if state['adopted_exit_code'] != 0 or adoption['frame'] not in {row['frame'] for row in rows}:
                raise RuntimeError('试帧未正常完成并校验，保留现场，不盲目开始整片')
        check_identity(batch, plan['identity_sha256'])
        previous_path = batch / 'queue-worker.json'
        if previous_path.exists():
            previous = json.loads(previous_path.read_text(encoding='utf-8'))
            if previous['identity_sha256'] != plan['identity_sha256']:
                raise ValueError('历史工作进程属于另一工程')
            try:
                observer = ProcessObservation(previous['pid'], plan['blender'], previous['created_filetime'])
            except OSError as error:
                if error.winerror not in (87, 1168):
                    raise
            if observer:
                # 调度器被关闭而Blender仍在跑时，续渲接管已有工作，
                # 绝不再开第二份去争抢同名帧。PID复用仍严格拒绝。
                active_pid = previous['pid']
                update('OBSERVING_EXISTING_FULL_SEQUENCE')
                while observer.alive():
                    time.sleep(10); update('OBSERVING_EXISTING_FULL_SEQUENCE')
                state['previous_worker_exit_code'] = observer.exit_code()
                observer.close(); observer = None; active_pid = None
        verified.clear()
        if len(update('CHECKING_RESUME')) == 2520:
            state.update(sequence_complete=True, status='FULL_SEQUENCE_VALIDATED')
            commit(state_path, state)
            return
        if shutil.disk_usage(batch).free < 12 * 1024**3:
            raise RuntimeError('可用空间不足12GiB，保留所有输出并停止')
        # 全部2520帧是真实渲染请求；旧脚本自行严格验证并跳过同源完成帧。
        # --python-exit-code避免Blender把Python失败退出码误报为0。
        stamp = time.strftime('%Y%m%d-%H%M%S')
        stdout_path = batch / ('sequence-' + stamp + '.stdout.log')
        stderr_path = batch / ('sequence-' + stamp + '.stderr.log')
        command = [plan['blender'], '-b', '--python-exit-code', '2', '--python', str(batch / 'film_scene.py'),
                   '--', '--out', str(batch), '--mode', identity['mode'], '--samples', str(identity['samples']),
                   '--frames', 'all']
        with stdout_path.open('wb') as stdout, stderr_path.open('wb') as stderr:
            producer = subprocess.Popen(command, cwd=batch, stdout=stdout, stderr=stderr,
                                        creationflags=subprocess.CREATE_NO_WINDOW)
            active_pid = producer.pid
            observer = ProcessObservation(producer.pid, plan['blender'])
            commit(previous_path, dict(author='mio', identity_sha256=plan['identity_sha256'],
                                       pid=producer.pid, created_filetime=observer.created, command=command))
            observer.close(); observer = None
            state.update(sequence_started_local=time.strftime('%Y-%m-%dT%H:%M:%S'),
                         command=command, stdout=str(stdout_path), stderr=str(stderr_path))
            update('RENDERING_FULL_SEQUENCE')
            while producer.poll() is None:
                time.sleep(10); update('RENDERING_FULL_SEQUENCE')
        state['sequence_exit_code'] = producer.returncode
        active_pid = None
        # 清空校验缓存，末次全部帧重新读CRC/DEFLATE/SHA；数量不能
        # 被重复清单记录凑满，也不把完成PNG序列冒充已编码视频/播放器。
        verified.clear(); rows = update('VERIFYING_FULL_SEQUENCE')
        if producer.returncode != 0 or len(rows) != 2520:
            raise RuntimeError('整片进程退出但未完整产出2520帧')
        state.update(sequence_complete=True, status='FULL_SEQUENCE_VALIDATED')
        commit(state_path, state)
    except Exception as error:
        # 未成功获取锁的第二个观察者不能覆盖第一份队列进度。
        state.update(status='FAILED_RETAINED_OUTPUT', error=repr(error), traceback=traceback.format_exc())
        state['active_owned_worker_alive'] = producer is not None and producer.poll() is None
        failure_path = batch / ('queue-failure-%d.json' % os.getpid())
        commit(failure_path, state)
        if owns_lock:
            commit(state_path, state)
        print(json.dumps(state, ensure_ascii=False), flush=True)
        raise
    finally:
        if observer:
            observer.close()
        if lock:
            lock.close()


if __name__ == '__main__':
    main()
