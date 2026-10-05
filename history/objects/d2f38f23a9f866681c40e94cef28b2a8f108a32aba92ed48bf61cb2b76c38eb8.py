#!/usr/bin/env python3
"""mio：固定版本libwebp的可审计构建适配，不修改第三方源快照。

VP8随机初始化的幅度参数是IEEE754单精度，解码器仅传1.0f；原库
使用浮点比较/乘法转Q8，而沙核任务不保存FPU状态。生成副本保留
上游版权、种子表、索引与函数ABI，只按原参数位模式完成同一Q8
转换。原文件SHA必须符合登记，版本升级不能静默套用文本替换。
"""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    relative = 'libwebp/src/utils/random_utils.c'
    source = ROOT / 'third_party' / relative
    raw = source.read_bytes()
    catalog = json.loads((ROOT / 'third_party/SOURCES.json').read_text(encoding='utf-8'))
    assert hashlib.sha256(raw).hexdigest() == catalog['files'][relative], '第三方随机源与固定版本不符'
    text = raw.decode('utf-8').replace('\r\n', '\n')
    old = '''  rg->amp = (dithering < 0.0) ? 0
            : (dithering > 1.0)
                ? (1 << VP8_RANDOM_DITHER_FIX)
                : (uint32_t)((1 << VP8_RANDOM_DITHER_FIX) * dithering);'''
    new = '''  /* mio：参数ABI仍是float，位复制不执行任何浮点运算。
   * [0,1)的幅度是floor(尾数*2^(指数-127-23+8))；0/负值为0，
   * >=1裁到256。指数太小时先返回0，避免移位超过32位。
   * 当前固定解码器实际仅以1.0f调用，原种子和随机序列不变。 */
  union { float scalar; uint32_t bits; } parameter;
  parameter.scalar = dithering;
  const uint32_t bits = parameter.bits;
  if (bits & 0x80000000u) rg->amp = 0;
  else if (bits >= 0x3f800000u) rg->amp = 1 << VP8_RANDOM_DITHER_FIX;
  else {
    const int shift = 127 + 23 - VP8_RANDOM_DITHER_FIX - (int)(bits >> 23);
    rg->amp = shift >= 32 ? 0 : (int)(((bits & 0x7fffffu) | 0x800000u) >> shift);
  }'''
    assert text.count(old) == 1, '固定适配点缺失或重复，停止而不是猜测'
    output = ROOT / 'build/codec/webp-adapt/random_utils.c'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text.replace(old, new), encoding='utf-8', newline='\n')
    record = dict(author='mio', source=relative, source_sha256=hashlib.sha256(raw).hexdigest(),
                  generated_sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
                  change='VP8InitRandom amplitude: IEEE754 bit pattern to Q8, no floating arithmetic')
    output.with_suffix('.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')
    # BGRA解码也链接多输出分支，因此RGB->YUV辅助代码不能仅靠GC
    # 自动排除。其gamma只用于库内编码/输出YUV，而服务只输出BGRA。
    # 构建副本关闭此未使用分支的gamma，保留原解码YUV->RGB转换。
    yuv_relative = 'libwebp/src/dsp/yuv.c'
    yuv_raw = (ROOT/'third_party'/yuv_relative).read_bytes()
    assert hashlib.sha256(yuv_raw).hexdigest() == catalog['files'][yuv_relative]
    yuv_text = yuv_raw.decode('utf-8').replace('\r\n', '\n')
    assert yuv_text.count('#define USE_GAMMA_COMPRESSION\n') == 1
    yuv_text = yuv_text.replace('#define USE_GAMMA_COMPRESSION\n',
        '/* mio：服务只用BGRA解码；不引入编码用的gamma/pow。 */\n')
    yuv_output = output.with_name('yuv.c')
    yuv_output.write_text(yuv_text, encoding='utf-8', newline='\n')
    yuv_output.with_suffix('.json').write_text(json.dumps(dict(author='mio',source=yuv_relative,
        source_sha256=hashlib.sha256(yuv_raw).hexdigest(),
        generated_sha256=hashlib.sha256(yuv_output.read_bytes()).hexdigest(),
        change='Disable unused RGB->YUV encoding gamma; retain decoder YUV->RGB'),indent=2)+'\n',encoding='utf-8')
    print('libwebp integer adapter:', record)


if __name__ == '__main__':
    main()
