"""正式GUI的薄JSON输入接线；外层拥有现成VM/Guest/Harness，本模块不启动它们。"""
from pathlib import Path
import contextlib
import hashlib
import json
import re
import sys
import time
import wave

sys.dont_write_bytecode=True


def digest(body):return hashlib.sha256(body).hexdigest()


class GuiRuntime:
    def __init__(self,flow,harness,*,session_parser,baseline=None,cold_font=False):
        """flow是现成FlowInput；baseline在创建测试SID之前由外层quiet/resources取得。"""
        self.flow,self.h=flow,harness
        self.session_parser=session_parser
        self.baseline,self.cold_font=baseline,cold_font
        self.state=harness.report.setdefault("p6_gui_runtime",dict(status="INTERACTIVE_UNVERIFIED",actions={},facts=[],session=None,test_session=None))
        self.finished=False

    def _persist(self):self.h.persist()

    def _ordinary(self):
        value=self.state["session"]
        if value is None:raise ValueError("尚未用真实sessionctl list绑定已选中的UID1000普通SID")
        return value

    def _screen_ref(self,value):
        """坐标只能引用本VM刚实际采集的截图，不从DIP布局表拼出点。"""
        if not isinstance(value,dict) or set(value)!={"path","sha256"}:raise ValueError("坐标输入需要实际screenshot path/SHA")
        candidates=[item for item in self.h.report.get("screenshots",[]) if item.get("path")==value["path"] and item.get("sha256")==value["sha256"]]
        if not candidates:raise ValueError("坐标没有本VM真实截图来源")
        path=Path(value["path"]).resolve(strict=True)
        if not path.is_relative_to(self.h.vm.out.resolve()) or digest(path.read_bytes())!=value["sha256"]:raise ValueError("截图真实字节改变或越过VM输出目录")
        return candidates[-1]

    def _point(self,value,screen):
        if not isinstance(value,list) or len(value)!=2 or any(type(v) is not int for v in value):raise ValueError("需要显式实际物理点[x,y]")
        if not 0<=value[0]<screen["width"] or not 0<=value[1]<screen["height"]:raise ValueError("点不属于当前截图尺寸")
        return tuple(value)

    def _local(self,label):
        if not isinstance(label,str) or not re.fullmatch(r"[A-Za-z0-9_.-]+",label) or label in (".",".."):raise ValueError("输出只接受新叶名")
        path=self.h.vm.out/label
        if path.exists():raise FileExistsError("不覆盖已有证据: "+str(path))
        return path

    def _guest_path(self,path):
        if not isinstance(path,str) or not path.startswith("/") or "\\" in path or "\0" in path or len(path.encode())>63:
            raise ValueError("需要真实绝对客体路径且不超过63B")
        return path

    def _captures(self):
        raw=self.h.vm.audio_hmp("info capture");rows={}
        for line in raw.splitlines():
            if not line.strip():continue
            match=re.fullmatch(r"\[([0-9]+)\]: Capturing audio\(48000,16,2\) to (.*): ([0-9]+) bytes",line.strip())
            if not match:raise ValueError("实际audio capture状态格式不同: "+raw)
            rows[int(match[1])]=dict(path=match[2],bytes=int(match[3]))
        return rows,raw

    def _audio_stop(self):
        vm=self.h.vm;index=vm.audio_capture_id
        if index is None:return dict(status="NO_SELF_OWNED_ACTIVE_CAPTURE",audio_device_kept=bool(vm.report.get("audio_enabled")))
        before,raw=self._captures()
        if index not in before:raise ValueError("self-owned音频capture index已不存在")
        path=Path(before[index]["path"]).resolve()
        if not path.is_relative_to(vm.out.resolve()):raise ValueError("capture文件不在本VM输出内")
        reply=vm.audio_hmp("stopcapture "+str(index));after,remaining=self._captures()
        if reply.strip() or index in after or set(after)!=(set(before)-{index}):raise ValueError("self-owned capture没有精确停止")
        vm.audio_capture_id=None
        vm.report.setdefault("audio_capture",{}).update(index=index,path=str(path),finalized=True,host_audible=False)
        vm.persist()
        with wave.open(str(path),"rb") as capture:
            if (capture.getnchannels(),capture.getsampwidth(),capture.getframerate())!=(2,2,48000):raise ValueError("实际PCM不是48k/S16/双声道")
            frames=capture.getnframes()
        result=dict(status="SELF_OWNED_CAPTURE_FINALIZED",index=index,path=str(path),bytes=path.stat().st_size,
            sha256=digest(path.read_bytes()),frames=frames,seconds=frames/48000,
            before_info=raw,remaining_info=remaining,audio_device_kept=True,host_audible=False,
            scope="REAL_QEMU_MIXER_PCM_NOT_HOST_AUDIBILITY_OR_SOUND_SEMANTICS_PASS")
        self.state.setdefault("audio_captures",[]).append(result);return result

    def _audio_start(self,label):
        vm=self.h.vm
        if not vm.report.get("audio_enabled"):raise ValueError("需要外层audio=True保留AC97，不能用无设备冒充播放")
        if vm.audio_capture_id is not None:raise ValueError("先精确停止已有self-owned capture")
        path=self._local(label);before,_=self._captures()
        reply=vm.audio_hmp('wavcapture "'+path.as_posix()+'" sand-audio 48000 16 2')
        after,raw=self._captures();created=set(after)-set(before)
        if reply.strip() or len(created)!=1:raise ValueError("未观察到唯一新capture")
        index=created.pop()
        if Path(after[index]["path"]).resolve()!=path.resolve():raise ValueError("新capture路径不同")
        vm.audio_capture_id=index
        vm.report["audio_capture"]=dict(transport="QEMU_MIXER_WAVCAPTURE",index=index,path=str(path),info=raw,finalized=False,host_audible=False)
        vm.persist()
        return dict(status="SELF_OWNED_CAPTURE_STARTED",index=index,path=str(path),audio_device_kept=True,host_audible=False,
            instruction="Sound短动作完成立即audio_stop，不覆盖整个长GUI流程")

    def request(self,request):
        if not isinstance(request,dict) or not isinstance(request.get("id"),str) or not request["id"]:raise ValueError("每次输入需要唯一id")
        ident,op=request["id"],request.get("op")
        fingerprint=digest(json.dumps(request,sort_keys=True,ensure_ascii=False).encode())
        prior=self.state["actions"].get(ident)
        if prior:
            if prior["request_sha256"]!=fingerprint:raise ValueError("相同id携带不同请求")
            # 不确定MOVE/COPY不能因传输重试而重发；重复id只返回原观察。
            return dict(prior,replayed=False)
        record=dict(id=ident,op=op,request_sha256=fingerprint,status="ATTEMPTING",seconds=time.monotonic(),qemu_pid=self.h.vm.process.pid)
        self.state["actions"][ident]=record;self._persist()
        try:
            if op=="run":
                command=request["command"]
                if not isinstance(command,str) or "\n" in command or "\r" in command:raise ValueError("run仅接受一条明确非秘密CLI命令")
                item=self.h.run("p6-cli-"+ident,command,expected=request.get("expected",0),timeout=request.get("timeout",180))
                # CLI可切SID，不能把先前普通桌面的绑定带到新选中桌面；
                # 下一次物理业务输入前重新用真实sessionctl list确认即可。
                self.state["session"]=None
                result={key:item.get(key) for key in ("command","exit_code","stdout_path","stdout_sha256","stdout_bytes","output","wall_seconds")}
                result.update(channel="EXTERNAL_SYSTEM_MANAGEMENT_CLI",counts_as_gui_operation=False)
            elif op=="bind":
                sid=request["session"]
                if type(sid) is not int or sid<=0:raise ValueError("需要实际SID")
                item=self.h.run("p6-bind-"+ident,"sessionctl list")
                rows=self.session_parser(item["output"].encode())
                found=[row for row in rows if row["id"]==sid and row["uid"]==1000 and row["type"]=="user" and row["active"]]
                if len(found)!=1:raise ValueError("该SID不是实际选中的UID1000普通会话")
                self.state["session"]=dict(uid=1000,realm=0,session=sid,evidence_case=item["name"],session_row=found[0])
                self.state["test_session"]=dict(self.state["session"])
                result=self.state["session"]
            elif op=="screen":
                label=request["label"];self._local(label+".png");self._local(label+".ppm")
                self.h.screen(label)
                result=dict(self.h.report["screenshots"][-1],counts_as_gui_pass=False)
            elif op=="get":
                path=self._guest_path(request["path"]);local=self._local(request["label"])
                body=self.h.file(path,"p6-read-"+ident,local=local)
                transfer=self.h.report["cases"][-1]
                result=dict(path=str(local),guest=path,bytes=len(body),sha256=digest(body),generation=transfer.get("generation"),channel="READ_ONLY_MANAGEMENT_FILE_SNAPSHOT")
            elif op=="audio_stop":result=self._audio_stop()
            elif op=="audio_start":result=self._audio_start(request["label"])
            elif op=="keys":
                session=self._ordinary();keys=request["keys"]
                if not isinstance(keys,list) or not keys or any(not isinstance(v,str) or not re.fullmatch(r"[a-z0-9_-]+",v) for v in keys):raise ValueError("需要真实HMP键名数组")
                if any("f12" in value.split("-") for value in keys):raise ValueError("正式产品不套SCUIDEMO的F12诊断")
                self.flow.keys(*keys);result=dict(keys=keys,session=session,channel="ACTUAL_HMP_PS2",effect="UNVERIFIED")
            elif op=="type":
                session=self._ordinary()
                if request.get("secret") is not False or request.get("purpose") not in ("path","filename","document","symbol","terminal_command"):
                    raise ValueError("type只处理明确非秘密ASCII路径/正文/符号，密码由外层已有保密通道处理")
                self.flow.type_ascii(request["text"])
                result=dict(bytes=len(request["text"].encode()),purpose=request["purpose"],session=session,channel="ACTUAL_HMP_PS2",effect="UNVERIFIED")
            elif op in ("click","drag"):
                session=self._ordinary();screen=self._screen_ref(request["screenshot"])
                if op=="click":
                    point=self._point(request["point"],screen);which=request.get("button","left")
                    if which not in ("left","right"):raise ValueError("只接受实际左/右键")
                    self.flow.inputs.click(*point,which=which)
                    result=dict(point=point,button=which)
                else:
                    start=self._point(request["start"],screen);end=self._point(request["end"],screen)
                    self.flow.drag(start,end);result=dict(start=start,end=end)
                result.update(session=session,screenshot=request["screenshot"],channel="ACTUAL_PS2",effect="UNVERIFIED")
            elif op=="finish":
                # 窗口正常关闭、Studio/Debug领完任务、切回原桌面和logout由
                # Root真实输入/已有sessionctl完成，这里只检查实际最终账。
                if self.baseline is None:raise ValueError("缺外层进入测试SID之前的真实baseline")
                if self.h.vm.audio_capture_id is not None:record["final_audio_capture"]=self._audio_stop()
                session=self.state["test_session"]
                if session is None:raise ValueError("从未绑定真实测试SID")
                listed=self.h.run("p6-final-sessions-"+ident,"sessionctl list")
                rows=self.session_parser(listed["output"].encode())
                if any(row["id"]==session["session"] for row in rows):raise ValueError("测试SID仍在，先正常关闭/领完/切回/logout")
                after,observed=self.h.quiet();resources=self.h.resources()
                before,observed_before,resources_before=self.baseline
                ledger,okay=self.h.ledger(before,after,resources_before,resources,self.cold_font)
                self.h.append(dict(name="p6-gui-single-final-owner-account",**ledger,before_observations=observed_before,after_observations=observed),okay)
                result=dict(ledger=ledger,session_removed=True,gui_workflows_still_require_evidence=True)
                self.finished=True;self.state["status"]="INPUT_SESSION_FINISHED_WORKFLOW_VERDICTS_EXTERNAL"
            else:raise ValueError("未知op；只接受run/bind/keys/type/click/drag/screen/get/audio_start/audio_stop/finish")
            record.update(status="EXECUTED_EFFECT_REQUIRES_EVIDENCE",result=result)
        except BaseException as error:
            record.update(status="UNVERIFIED_OR_INPUT_UNCERTAIN",error=dict(type=type(error).__name__,message=str(error)))
        self._persist();return record


def serve(runtime,reader=None,writer=None):
    """Root持有外层QemuSession；JSONL stdout只传控制回复，现有harness打印走stderr。"""
    reader=reader or sys.stdin;writer=writer or sys.stdout
    for line in reader:
        try:
            request=json.loads(line)
            with contextlib.redirect_stdout(sys.stderr):result=runtime.request(request)
        except BaseException as error:result=dict(status="INVALID_REQUEST",error=dict(type=type(error).__name__,message=str(error)))
        writer.write(json.dumps(result,ensure_ascii=False)+"\n");writer.flush()
        if runtime.finished:break
