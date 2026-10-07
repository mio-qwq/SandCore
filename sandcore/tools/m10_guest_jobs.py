"""M10验收中的交互Shell作业等待；只操作创建作业的原Shell。

Guest.run为了取得精确字节会启动新的sh；它不拥有交互父Shell的
后台表，不能等待父Shell的$!。这里只读回独立重定向的结果文件。
"""


def wait_background(guest, selector, timeout=90):
    selector = int(selector)
    if selector <= 0:
        raise ValueError('后台作业选择值必须为正数')
    command = f'wait {selector} > /TMP/M10WAITOUT 2> /TMP/M10WAITERR'
    code, transcript, wall, cpu = guest.command(command, timeout)
    output = guest.get_bytes('/TMP/M10WAITOUT', 'same-shell-wait-output')
    error = guest.get_bytes('/TMP/M10WAITERR', 'same-shell-wait-error')
    return code, output, transcript+error, wall, cpu
