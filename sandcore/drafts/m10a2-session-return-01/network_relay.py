"""临时把WSL的单次发布命令接到现有本机代理，不改VPN或全局配置。"""
import socket,socketserver,select,json
from pathlib import Path
class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        try:
            with socket.create_connection(('127.0.0.1',7890),timeout=10) as upstream:
                peers=(self.request,upstream)
                while True:
                    ready,_,_=select.select(peers,[],[],60)
                    if not ready:return
                    for source in ready:
                        body=source.recv(65536)
                        if not body:return
                        (upstream if source is self.request else self.request).sendall(body)
        except (OSError,TimeoutError):pass
class Server(socketserver.ThreadingTCPServer):
    daemon_threads=True
with Server(('172.27.160.1',0),Handler) as server:
    meta={'host':server.server_address[0],'port':server.server_address[1]}
    Path(__file__).with_name('relay.json').write_text(json.dumps(meta),encoding='ascii')
    print(json.dumps(meta),flush=True)
    server.serve_forever()
