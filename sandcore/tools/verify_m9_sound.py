#!/usr/bin/env python3
"""M9默认六音效、声道资源与真实关机的双盘Windows QEMU证据。"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import time
import wave

from scserial import QemuSession, sha
from verify_m9 import Guest, Suite, png_from_ppm

ROOT = Path(__file__).resolve().parents[1]
EFFECTS = ('START', 'STOP', 'NOTICE', 'ERROR', 'COMPLETE', 'QUESTION')


def finish_capture(vm):
    if vm.audio_capture_id is None:
        raise RuntimeError('缺少活动音频捕获')
    response = vm.audio_hmp('stopcapture '+str(vm.audio_capture_id))
    if response.strip() or 'Capturing audio(' in vm.audio_hmp('info capture'):
        raise RuntimeError('真实音频捕获没有结束')
    vm.audio_capture_id = None
    vm.report['audio_capture']['finalized'] = True


def begin_capture(vm, path):
    if vm.audio_capture_id is not None:
        raise RuntimeError('上一音频捕获尚未结束')
    if vm.audio_hmp(f'wavcapture "{path.as_posix()}" sand-audio 48000 16 2').strip():
        raise RuntimeError('新音频捕获失败')
    match = re.fullmatch(r'\[([0-9]+)\]: Capturing audio\(48000,16,2\) to '+re.escape(path.as_posix())+r': [0-9]+ bytes',
                         vm.audio_hmp('info capture').strip())
    if not match:
        raise RuntimeError('新音频捕获实际状态错误')
    vm.audio_capture_id = int(match[1])


def pcm(path):
    with wave.open(str(path), 'rb') as sound:
        if sound.getparams()[:3] != (2, 2, 48000):
            raise ValueError('捕获或参考不是真实48k/S16/双声道')
        return sound.readframes(sound.getnframes())


def compare_effect(capture, reference):
    data, expected = pcm(capture), pcm(reference)
    blocks = []
    previous = None
    missing, discontinuities = [], []
    # 逐256帧选64帧真实波形锚点，而非只判断文件非零；不忽略中段断音。
    # 全零尾巴不参与唯一定位，完整PCM文件及摘要仍保留供试听/另行比较。
    for frame in range(256, len(expected)//4-64, 256):
        signature = expected[frame*4:(frame+64)*4]
        if not any(signature):
            continue
        at = data.find(signature, previous[1]*4 if previous else 0)
        if at < 0 or at % 4:
            missing.append(frame)
            continue
        position = at//4
        if previous:
            extra = position-previous[1]-(frame-previous[0])
            if extra:
                discontinuities.append(dict(reference_frame=frame, extra_frames=extra,
                                            extra_seconds=extra/48000))
        blocks.append([frame, position])
        previous = frame, position
    return dict(capture=str(capture), capture_sha256=sha(capture), frames=len(data)//4,
                reference=str(reference), reference_sha256=sha(reference), reference_frames=len(expected)//4,
                matched_blocks=len(blocks), missing_blocks=missing, discontinuities=discontinuities,
                waveform_present=bool(blocks) and not missing,
                contiguous=bool(blocks) and not missing and not discontinuities)


def run(boot, data, out, sounds, audio=True):
    report = dict(status='RUNNING', scope='DEFAULT_EFFECT_VOICE_RESOURCE_AND_POWER_ONLY',
                  cases=[], screenshots=[], audio_device=audio, effects=[])
    with QemuSession(boot, data, out, 'tcg', True, audio=audio, no_shutdown=True) as vm:
        guest = Guest(vm)
        suite = Suite(guest, report, {})
        try:
            guest.connect()
            if audio:
                # 原生编译也给开机音效足够时间完整输出，不先截断录制。
                guest.put_bytes((ROOT/'tests/m9/SOUNDBOUNDS.c').read_bytes(), '/TMP/SOUNDBOUNDS.C', 'sound-probe')
                suite.case('native-sound-bounds-compile', 's3c /TMP/SOUNDBOUNDS.C /TMP/SOUNDBOUNDS.SCX', timeout=240)
                suite.case('voice-parameters-quota-ownership-pause-flush-stale', '/TMP/SOUNDBOUNDS.SCX bounds',
                           contains=b'PASS voice parameters quota pause flush stale token reclaim')
                suite.case('voice-owner-exit-1', '/TMP/SOUNDBOUNDS.SCX leave', contains=b'PASS exit probe left two owned voices')
                suite.case('voice-owner-exit-slot-reuse', '/TMP/SOUNDBOUNDS.SCX leave', contains=b'PASS exit probe left two owned voices')
                finish_capture(vm)
                boot_audio = compare_effect(vm.out/'audio.wav', sounds/'START.WAV')
                report['boot_audio'] = boot_audio
                if not boot_audio['waveform_present']:
                    raise AssertionError('开机录音缺失原创音效波形')
                for kind, name in enumerate(EFFECTS):
                    capture = vm.out/('effect-'+name+'.wav')
                    begin_capture(vm, capture)
                    raw = suite.case('actual-effect-'+name, f'/TMP/SOUNDBOUNDS.SCX effect {kind}')
                    if len(raw) != 64 or struct.unpack('<16I', raw)[11]:
                        raise AssertionError('音效结束后真实DMA队列没有排空')
                    # 完成的QEMU混音块仍可能在本轮音频定时器里，等真实时钟一轮。
                    time.sleep(.1)
                    finish_capture(vm)
                    result = compare_effect(capture, sounds/(name+'.WAV'))
                    report['effects'].append(result)
                    if not result['contiguous']:
                        raise AssertionError(name+'音效没有连续输出参考波形')
                shutdown = vm.out/'shutdown.wav'
                begin_capture(vm, shutdown)
            screenshot = vm.out/'before-poweroff.ppm'
            vm.hmp('sendkey shift')
            vm.hmp('screendump "'+screenshot.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(screenshot))
            before = time.monotonic()
            code, _, _, _ = guest.command('poweroff')
            if code:
                raise AssertionError('外部SYSTEM关机请求被拒绝')
            # no-shutdown只保留已真实关机的QEMU现场，不替代客体电源指令。
            deadline = time.monotonic()+15
            while True:
                status = vm.qmp('query-status')
                if status.get('status') == 'shutdown':
                    break
                if time.monotonic() >= deadline:
                    raise TimeoutError('客体没有真实触发QEMU关机')
                time.sleep(.05)
            report['poweroff'] = dict(status=status, wall_seconds=time.monotonic()-before)
            if audio:
                finish_capture(vm)
                report['shutdown_audio'] = compare_effect(shutdown, sounds/'STOP.WAV')
                if not report['shutdown_audio']['contiguous']:
                    raise AssertionError('关机声没有在实际停机前完整输出')
            report['boot_continuity'] = 'PASS' if not audio or report['boot_audio']['contiguous'] else 'FAIL'
            if report['boot_continuity'] == 'FAIL':
                raise AssertionError('开机声在初始化/首帧阶段出现录音断续')
            report['status'] = 'SOUND_BOUNDARY_CASES_PASS_REMAINDER_PENDING'
        except BaseException as error:
            report.update(status='FAIL', failure=repr(error))
            raise
        finally:
            suite.persist()
            guest.close()
    if vm.report['exit_code'] != 0 or not all(vm.report['source_unchanged'].values()):
        raise AssertionError('声音验收退出失败或源盘被写')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot', type=Path, required=True)
    parser.add_argument('--data', type=Path, action='append', required=True)
    parser.add_argument('--sounds', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if len(args.data) < 2 or len(set(p.resolve() for p in args.data)) != len(args.data):
        parser.error('至少两个不同来源的数据盘')
    args.out.mkdir(parents=True, exist_ok=False)
    matrix = dict(status='RUNNING', runs=[], not_covered=['external MP3 encoders/VBR', 'all GUI controls', 'DMA hardware fault recovery'])
    try:
        for i, data in enumerate(args.data, 1):
            matrix['runs'].append(run(args.boot, data, args.out/f'disk-{i}', args.sounds.resolve()))
            matrix['runs'].append(run(args.boot, data, args.out/f'disk-{i}-no-audio', args.sounds.resolve(), False))
        matrix['status'] = 'SOUND_BOUNDARY_CASES_PASS_REMAINDER_PENDING'
    except BaseException as error:
        matrix.update(status='FAIL', failure=repr(error))
        raise
    finally:
        (args.out/'matrix.json').write_text(json.dumps(matrix, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
