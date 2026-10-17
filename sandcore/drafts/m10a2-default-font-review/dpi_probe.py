"""只观察已安装前端的Windows DPI声明；无盘、暂停，不修改宿主设置。"""
import ctypes as C
from ctypes import wintypes as W
from pathlib import Path
import json
import socket
import subprocess
import time

root=Path(__file__).resolve().parent
u=C.WinDLL('user32');k=C.WinDLL('kernel32');sh=C.WinDLL('shcore')
k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE
k.CloseHandle.argtypes=[W.HANDLE]
u.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
u.GetDpiForWindow.argtypes=[W.HWND];u.GetDpiForWindow.restype=W.UINT
sh.GetProcessDpiAwareness.argtypes=[W.HANDLE,C.POINTER(C.c_int)]
callback=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
def measure(pid):
    rows=[]
    def each(hwnd, unused):
        owner=W.DWORD();u.GetWindowThreadProcessId(hwnd,C.byref(owner))
        if owner.value==pid and u.IsWindowVisible(hwnd):
            title=C.create_unicode_buffer(512);u.GetWindowTextW(hwnd,title,512)
            rows.append(dict(hwnd=hwnd,title=title.value,dpi=u.GetDpiForWindow(hwnd)))
        return True
    cb=callback(each);u.EnumWindows(cb,0)
    h=k.OpenProcess(4096,False,pid);awareness=C.c_int(-1)
    try:sh.GetProcessDpiAwareness(h,C.byref(awareness))
    finally:k.CloseHandle(h)
    return dict(pid=pid,awareness=awareness.value,windows=rows)

with socket.socket() as reservation:
    reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
command=[r'C:\Program Files\qemu\qemu-system-i386.exe','-S','-m','128','-vga','std',
         '-display','sdl,window-close=on','-monitor','none','-serial','none','-nic','none',
         '-qmp',f'tcp:127.0.0.1:{port},server=on,wait=off']
p=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
record=dict(command=command,scope='PAUSED_DISKLESS_ORIGINAL_SDL_DPI_DECLARATION')
try:
    deadline=time.monotonic()+10
    while True:
        try:connection=socket.create_connection(('127.0.0.1',port),timeout=1);break
        except OSError:
            if p.poll() is not None or time.monotonic()>deadline:raise
            time.sleep(.1)
    with connection:
        stream=connection.makefile('rwb');record['greeting']=json.loads(stream.readline())
        for operation in ('qmp_capabilities','query-status'):
            stream.write((json.dumps(dict(execute=operation,id=operation))+'\n').encode());stream.flush()
            while True:
                response=json.loads(stream.readline())
                if response.get('id')==operation:break
        record['observation']=measure(p.pid)
        stream.write(b'{"execute":"quit","id":"quit"}\n');stream.flush()
    out,err=p.communicate(timeout=10);record.update(exit_code=p.returncode,stderr=err.decode(errors='replace'))
finally:
    if p.poll() is None:p.terminate();p.wait(timeout=10)
(root/'original-sdl-dpi.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
print(json.dumps(record['observation']))
