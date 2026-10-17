"""仅给Git的私有凭据管道响应；不落盘令牌，不向日志打印令牌。"""
import subprocess,sys
if len(sys.argv)>1 and sys.argv[1]=='get':
    fields=dict(line.rstrip('\n').split('=',1) for line in sys.stdin if '=' in line)
    if fields.get('host')!='github.com' or fields.get('protocol')!='https':sys.exit(0)
    r=subprocess.run(['wsl.exe','gh','auth','token','--hostname','github.com'],capture_output=True)
    if r.returncode:sys.exit(1)
    token=r.stdout.decode().strip()
    if not token or '\n' in token:sys.exit(1)
    sys.stdout.write('username=x-access-token\npassword='+token+'\n\n')
