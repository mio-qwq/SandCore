#!/usr/bin/env python3
"""生成SandCore原创短音效；数学合成属于宿主资源流程，内核无浮点。"""
import argparse
import json
import math
from pathlib import Path
import struct
import wave

RATE = 48000
SCORES = {
    'START': [(0, .46, 523.25, .17), (.12, .52, 659.25, .14), (.28, .68, 783.99, .12), (.50, .75, 1046.5, .09)],
    'STOP': [(0, .45, 783.99, .15), (.16, .48, 659.25, .13), (.34, .66, 523.25, .12)],
    'NOTICE': [(0, .30, 880, .16), (.14, .38, 1174.66, .10)],
    'ERROR': [(0, .16, 329.63, .14), (.20, .28, 293.66, .12)],
    'COMPLETE': [(0, .20, 659.25, .14), (.12, .24, 783.99, .12), (.25, .47, 1046.5, .11)],
    'QUESTION': [(0, .24, 659.25, .13), (.18, .38, 739.99, .12)],
}


def render(notes):
    frames = math.ceil((max(start + duration for start, duration, _, _ in notes) + .15) * RATE)
    stereo = [0.0] * (frames * 2)
    for index, (start, duration, frequency, amplitude) in enumerate(notes):
        pan = .5 + (index % 3 - 1) * .12
        first, count = round(start * RATE), round(duration * RATE)
        for i in range(count):
            time = i / RATE
            attack = min(1.0, time / .009)
            release = min(1.0, (duration-time) / .070)
            envelope = attack * release * math.exp(-3.3*time/duration)
            phase = 2*math.pi*frequency*time
            tone = math.sin(phase) + .16*math.sin(2*phase) + .055*math.sin(3*phase)
            sample = amplitude * envelope * tone
            stereo[(first+i)*2] += sample * math.sqrt(1-pan)
            stereo[(first+i)*2+1] += sample * math.sqrt(pan)
    peak = max(abs(x) for x in stereo)
    if peak >= .9:
        raise ValueError('原创混音超出峰值预算')
    output = bytearray(frames * 4)
    for i, sample in enumerate(stereo):
        struct.pack_into('<h', output, i*2, round(sample * 32767))
    return output, frames, peak


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    report = {}
    for name, notes in SCORES.items():
        body, frames, peak = render(notes)
        with wave.open(str(args.out / (name + '.WAV')), 'wb') as sound:
            sound.setnchannels(2)
            sound.setsampwidth(2)
            sound.setframerate(RATE)
            sound.writeframes(body)
        report[name] = dict(frames=frames, seconds=frames/RATE, peak=peak, bytes=len(body)+44)
    (args.out / 'MANIFEST.JSON').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
