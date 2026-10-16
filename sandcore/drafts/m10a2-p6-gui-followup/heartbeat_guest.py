"""下一轮GUI使用的有限心跳等待，不恢复或清除已失败的管理会话。"""
import json
import threading
import time
from serial_protocol import ManagementResultError
from verify_m10a2_ui_gui import GuiGuest

class FollowupGuest(GuiGuest):
    def connect(self, heartbeat=True):
        super().connect(heartbeat=False)
        self.heartbeat_stop=threading.Event()
        self.heartbeat_log=(self.vm.out/'heartbeat.jsonl').open('x',encoding='utf-8')
        if not heartbeat:return
        def beat():
            try:
                while not self.heartbeat_stop.wait(5):
                    tail=bytes(self.debug)
                    if tail.rfind(b'STOP ')>tail.rfind(b'OK resume'):continue
                    started=time.monotonic()
                    record=dict(kind=10,started_monotonic=started,per_attempt_timeout=15,
                        maximum_attempts=3,actual_response_required=True)
                    try:
                        # GUI03真实PING超时并在之后收到同序号成功ACK。
                        # 只延长这个幂等心跳的有限等待，业务请求/真实收账仍严格。
                        result,body=self.client.request(10,timeout=15)
                        record.update(result=result,reply_bytes=len(body),sequence=self.client.sequence)
                        if result<0:raise ManagementResultError(10,result)
                    except BaseException as error:
                        record.update(error=dict(type=type(error).__name__,message=str(error)))
                        raise
                    finally:
                        record['elapsed_seconds']=time.monotonic()-started
                        self.heartbeat_log.write(json.dumps(record,ensure_ascii=False)+'\n')
                        self.heartbeat_log.flush()
            except BaseException as error:
                if not self.stop.is_set():
                    self.errors.append(error)
                    self.client.fail(error)
        self.heart=threading.Thread(target=beat,name='P6-GUI-heartbeat')
        self.heart.start()

    def close(self):
        # 先让本轮在途心跳完成，reader此时仍活着接收真实ACK；不靠清failure退出。
        if hasattr(self,'heartbeat_stop'):
            self.heartbeat_stop.set()
            if self.heart:self.heart.join(timeout=60)
        try:
            super().close()
        finally:
            if hasattr(self,'heartbeat_log') and (not self.heart or not self.heart.is_alive()):
                self.heartbeat_log.close()
