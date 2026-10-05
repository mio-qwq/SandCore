"""宿主串口传输显示：ACK正文进度、实际速度和剩余时间；不改客体协议。"""
import sys
import shutil
import threading
import time
import unicodedata


class TransferProgress:
    PHASES = {'prepare': '读取快照', 'hash': '计算SHA256', 'transfer': '传输',
              'commit': '校验并提交', 'verify': '校验并保存', 'done': '完成'}

    def __init__(self, direction, name, stream=None, lock=None):
        self.direction, self.name = direction, name
        self.stream = stream or sys.stdout
        self.lock = lock or threading.RLock()
        self.tty = self.stream.isatty()
        self.phase, self.line = None, ''
        self.last_draw = self.began = time.monotonic()
        self.transfer_began = None
        self.active, self.final = False, False
        self.columns = max(40, shutil.get_terminal_size((80,24)).columns)
        self.previous_width = 0

    @staticmethod
    def width(text):
        return sum(2 if unicodedata.east_asian_width(c) in ('W','F') else 1 for c in text)

    def fit(self, text):
        result = ''
        for c in text:
            if self.width(result+c) >= self.columns:
                break
            result += c
        return result

    @staticmethod
    def amount(count):
        if count >= 1024*1024:
            return f'{count/(1024*1024):.2f} MiB'
        if count >= 1024:
            return f'{count/1024:.1f} KiB'
        return f'{count} B'

    @staticmethod
    def duration(seconds):
        seconds = int(max(0, seconds)+.5)
        return f'{seconds//3600:d}:{seconds//60%60:02d}:{seconds%60:02d}' if seconds >= 3600 else f'{seconds//60:02d}:{seconds%60:02d}'

    def __call__(self, phase, done, total):
        now = time.monotonic()
        if phase == 'transfer' and self.transfer_began is None:
            self.transfer_began = now
        changed = phase != self.phase
        self.phase = phase
        # 控制台每秒最多四次；日志每秒一次，单帧回调不会放大IO成本。
        if not changed and now-self.last_draw < (.25 if self.tty else 1):
            return
        self.last_draw = now
        if total is None:
            text = f'{self.direction} | {self.PHASES[phase]}'
        else:
            fraction = min(1, max(0, done/total)) if total else (1 if phase == 'done' else 0)
            slots = 20 if self.columns >= 100 else 10
            filled = int(fraction*slots)
            bar = '='*filled+'-'*(slots-filled)
            text = f'{self.direction} [{bar}] {fraction*100:5.1f}% {self.amount(done).replace(" ","")}/{self.amount(total).replace(" ","")}'
            if phase in ('transfer', 'commit', 'verify', 'done'):
                elapsed = now-(self.transfer_began or now)
                speed = done/elapsed if elapsed > .001 else 0
                eta = self.duration((total-done)/speed) if speed and done<total else '00:00' if phase=='done' else '--:--'
                text += f' {speed/1024:.1f}KiB/s ETA {eta}'
            text += ' '+self.PHASES[phase]
        with self.lock:
            if not self.active:
                self.stream.write(f'\n{self.direction} {self.name}\n')
                self.active = True
            self.line = self.fit(text) if self.tty else text
            width = self.width(self.line)
            self.stream.write(('\r'+self.line+' '*max(0,self.previous_width-width)) if self.tty else text+'\n')
            self.previous_width = width
            if phase == 'done':
                if self.tty:
                    self.stream.write('\n')
                self.active, self.final = False, True
            self.stream.flush()

    def external(self, data):
        # 串口输出和COM1暂停提示不能钻进进度条正文；短暂换行后重画。
        with self.lock:
            if self.active and self.tty:
                self.stream.write('\r'+' '*self.previous_width+'\r')
                self.stream.flush()
            self.stream.buffer.write(data)
            self.stream.buffer.flush()
            if self.active and self.tty:
                if not data.endswith(b'\n'):
                    self.stream.write('\n')
                self.stream.write(self.line)
                self.stream.flush()

    def fail(self):
        with self.lock:
            if self.active:
                self.stream.write(('\n' if self.tty else '')+f'{self.direction} 未完成，未标记成功\n')
                self.stream.flush()
                self.active = False
