#!/usr/bin/env python3
"""M9固定CPU能力/缺音频设备回退，实际双盘运行，保留全部失败。"""
import argparse
import json
from pathlib import Path
import struct
import re
from scserial import QemuSession
from verify_m9 import Guest,Suite,png_from_ppm

CAPS=b'#include "SCIO.H"\nint main(void){u32 out[8];if(sc_simd_info(out)<0)return 1;return cli_write(1,out,32)<0?1:0;}\n'
PROFILES=(('no-sse2','qemu32,-sse2',(1,1,0,512)),
          ('fnsave','qemu32,-fxsr,-sse,-sse2',(1,0,0,108)),
          ('no-fpu','qemu32,-fpu,-fxsr,-sse,-sse2',(0,0,0,0)))


def run(boot,data,out,cpu,expected,audio=True):
    report=dict(status='RUNNING',scope='CPU_DEVICE_FALLBACK_CASES_ONLY',cases=[],screenshots=[],cpu_profile=cpu,audio_device=audio)
    with QemuSession(boot,data,out,'tcg',True,audio=audio,cpu=cpu) as vm:
        guest=Guest(vm)
        suite=Suite(guest,report,{})
        try:
            guest.connect()
            guest.put_bytes(CAPS,'/TMP/FPUCAPS.C','capabilities')
            suite.case('native-capabilities-compile','s3c /TMP/FPUCAPS.C /TMP/FPUCAPS.SCX',timeout=240)
            raw=suite.case('capabilities-binary-stdout','/TMP/FPUCAPS.SCX')
            if len(raw)!=32:
                raise AssertionError('SIMDINFO快照长度异常')
            fields=struct.unpack('<8I',raw)
            if fields[0]!=1 or (fields[1],fields[2],fields[3],fields[5])!=expected:
                raise AssertionError('客体实际CPU能力/保存格式不符合固定配置: '+repr(fields))
            report['capabilities']=fields
            if audio:
                suite.case('native-scalar-helper-compile','s3c /SYS/TEST/M9CHECK.C /TMP/M9CHECK.SCX',timeout=240)
                suite.case('no-SSE2-real-scalar-fallback','/TMP/M9CHECK.SCX simd',contains=b'PASS scalar add')
                if expected[0]:
                    method=b'FNSAVE' if not expected[1] else b'FXSAVE'
                    result=suite.case('x87-concurrent-preemption','/SYS/TEST/FPUSTATE.SCX 7 > /TMP/M9F1 &\n/SYS/TEST/FPUSTATE.SCX 13 > /TMP/M9F2 &\nwait\ncat /TMP/M9F1 /TMP/M9F2',contains=b'PASS x87 '+method,timeout=120)
                    expression=rb'PASS x87 '+method+rb' initial isolation and preemption [1-9][0-9]*'
                    lines=result.splitlines()
                    if len(lines)!=2 or any(not re.fullmatch(expression,line) for line in lines):
                        raise AssertionError('并发x87两项必须分别完整通过，不能只认其中一项')
                    suite.case('x87-slot-reuse','/SYS/TEST/FPUSTATE.SCX 21',contains=b'PASS x87 '+method)
                    suite.case('MP3-without-SSE2','soundplay /SYS/TEST/TONE.MP3',b'',timeout=30)
                else:
                    suite.case('no-x87-fixture-guard','/SYS/TEST/FPUSTATE.SCX 7',b'UNSUPPORTED x87 state fixture\n',status=77)
                    suite.case('MP3-no-FPU-clean-denial','soundplay /SYS/TEST/TONE.MP3',status=1,timeout=30)
                suite.case('integer-WAV-fallback','soundplay /SYS/SOUND/NOTICE.WAV',b'',timeout=30)
                suite.case('PCM-queue-without-SSE2','/TMP/M9CHECK.SCX audio',contains=b'PASS PCM queue drained')
            else:
                suite.case('no-device-WAV-clean-denial','soundplay /SYS/SOUND/NOTICE.WAV',status=1,timeout=30)
                suite.case('no-device-MP3-clean-denial','soundplay /SYS/TEST/TONE.MP3',status=1,timeout=30)
            suite.case('Shell-still-alive-after-fallbacks','echo M9-FALLBACK-ALIVE',b'M9-FALLBACK-ALIVE\n')
            screenshot=vm.out/'fallback.ppm'
            vm.hmp('screendump "'+screenshot.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(screenshot))
            report['status']='FALLBACK_CASES_PASS_REMAINDER_PENDING'
        except BaseException as error:
            report.update(status='FAIL',failure=repr(error))
            raise
        finally:
            suite.persist()
            guest.close()
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot',type=Path,required=True)
    parser.add_argument('--data',type=Path,required=True,action='append')
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    if len(args.data)<2 or len(set(p.resolve() for p in args.data))!=len(args.data):
        parser.error('传入至少两份不同来源的测试盘')
    args.out.mkdir(parents=True,exist_ok=False)
    matrix=dict(status='RUNNING',scope='FIXED_CPU_AND_NO_AUDIO_DEVICE_MATRIX',runs=[])
    try:
        for i,data in enumerate(args.data,1):
            for name,cpu,expected in PROFILES:
                matrix['runs'].append(run(args.boot,data,args.out/f'disk-{i}-{name}',cpu,expected))
            matrix['runs'].append(run(args.boot,data,args.out/f'disk-{i}-no-audio','qemu32',(1,1,1,512),False))
        matrix['status']='FALLBACK_CASES_PASS_REMAINDER_PENDING'
    except BaseException as error:
        matrix.update(status='FAIL',failure=repr(error))
        raise
    finally:
        (args.out/'matrix.json').write_text(json.dumps(matrix,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
