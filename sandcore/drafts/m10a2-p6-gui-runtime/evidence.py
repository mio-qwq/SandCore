"""只解释实际取回/观察的产品证据；没有夹具尺寸、固定帧数或默认PASS。"""
import hashlib
import shlex
import struct
import zlib


def sha(body):return hashlib.sha256(body).hexdigest()


def journal(body,*,session):
    if len(body)<64 or body[:8]!=b"SCSFJ1\0\0":raise ValueError("恢复日志头不完整")
    h=struct.unpack_from("<14I",body,8)
    if h[:2]!=(64,1) or h[6]!=1000 or h[8]!=0 or h[9]!=session or h[10:13]!=(0,0,0) or zlib.crc32(body[:60])!=h[13]:
        raise ValueError("恢复头版本/普通SID/CRC不同")
    if not h[4] or not h[5]:raise ValueError("日志未记录worker身份")
    rows=[];at=64
    while at<len(body):
        if len(body)-at<24:raise ValueError("恢复frame截断")
        size,version,kind,seq,n,crc=struct.unpack_from("<6I",body,at)
        if (size,version,n)!=(256,1,232) or seq!=len(rows)+1 or size>len(body)-at:raise ValueError("恢复frame版本/连续序号/范围错误")
        data=body[at+24:at+size]
        if zlib.crc32(data,zlib.crc32(body[at:at+20]))!=crc:raise ValueError("恢复frame CRC不同")
        r=struct.unpack_from("<24I",data)
        if r[:2]!=(232,1) or r[2:4]!=h[2:4] or r[20:22]!=h[4:6] or r[22:]!=(0,0):raise ValueError("恢复ACK nonce/worker/版本不同")
        def path(offset):
            raw=data[offset:offset+68];end=raw.find(b"\0")
            if end<0 or any(raw[end:]):raise ValueError("ACK路径没有唯一NUL/零尾部")
            return raw[:end].decode("utf-8")
        row=dict(type=kind,sequence=seq,nonce=list(r[2:4]),item=r[4],operation=r[5],state=r[6],stage=r[7],
            status=struct.unpack_from("<i",data,32)[0],cause=struct.unpack_from("<i",data,36)[0],domain=r[10],
            raw=struct.unpack_from("<i",data,44)[0],bytes=r[12]|r[13]<<32,source_object=r[14],source_revision=r[15],
            destination_object=r[16],destination_revision=r[17],conflict_epoch=r[18],flags=r[19],
            worker_pid=r[20],worker_generation=r[21],source=path(96),destination=path(164))
        if row["state"]==6 and row["flags"]&1:raise ValueError("UNCERTAIN不能同时声称DURABLE")
        rows.append(row);at+=size
    if not rows or rows[0]["type"]!=0x100:raise ValueError("缺真实START")
    finished=rows[-1]["type"]==0x101
    return dict(format="SCSFJ1",sha256=sha(body),bytes=len(body),uid=1000,realm=0,session=session,
        worker_pid=h[4],worker_generation=h[5],nonce=list(h[2:4]),header_crc=True,frame_crcs=True,
        complete=finished,rows=rows,
        scope="PERSISTED_WORKER_ACK_CRC_NOT_CRYPTOGRAPHIC_AUTHORIZATION")


def transfer(journal_info,*,operation,source,destination,expected_body,source_before,source_after,destination_after,worker_identity=None):
    """FileInfo由外层真实只读查询给出，缺源/目标观察保持未验证。"""
    matches=[r for r in journal_info["rows"] if r["item"] and r["operation"]==operation and r["source"]==source and r["destination"]==destination]
    if not matches:return dict(verdict="UNVERIFIED",missing=["matching actual ACK"])
    row=matches[-1];wanted=sha(expected_body);missing=[]
    if not journal_info["complete"]:missing.append("FINISH")
    if row["state"]!=1 or row["status"]<0 or not row["flags"]&1 or not row["destination_revision"]:missing.append("DONE+DURABLE successful ACK")
    if row["stage"] not in ({3} if operation==1 else {4,5} if operation==2 else set()):missing.append("operation terminal stage")
    if row["bytes"]!=len(expected_body):missing.append("actual byte count")
    if not source_before or source_before.get("sha256")!=wanted or source_before.get("uid")!=1000:missing.append("ordinary source-before bytes/owner")
    if not destination_after or destination_after.get("sha256")!=wanted or destination_after.get("revision")!=row["destination_revision"]:missing.append("destination bytes/revision")
    if operation==1 and (not source_before or not source_after or any(source_after.get(k)!=source_before.get(k) for k in ("sha256","revision","uid","gid","mode"))):missing.append("COPY retains exact source")
    if operation==2 and (not source_after or source_after.get("exists") is not False):missing.append("MOVE source absence")
    if worker_identity is not None and (worker_identity.get("pid"),worker_identity.get("generation"))!=(row["worker_pid"],row["worker_generation"]):missing.append("actual worker identity binding")
    return dict(verdict="UNVERIFIED" if missing else "OBSERVED_AND_CONSISTENT",missing=missing,ack=row,
        expected_sha256=wanted,worker_identity="NOT_OBSERVED_EXTERNALLY" if worker_identity is None else worker_identity,
        scope="FILE_OPERATION_NOT_PROOF_OF_GUI_TRIGGER_OR_DROP_PROTOCOL")


def cancelled_drop(before,after,*,cancel_input_evidence):
    missing=[]
    if not cancel_input_evidence:missing.append("actual cancel input")
    if set(before)!=set(after) or any(before[name]!=after.get(name) for name in before):missing.append("both endpoints unchanged including revisions")
    return dict(verdict="UNVERIFIED" if missing else "OBSERVED_AND_CONSISTENT",missing=missing,
        scope="CANCEL_DATA_INVARIANCE_CAPTURE_AND_SELECTION_MUST_BE_OBSERVED_SEPARATELY")


def studio_build(text,*,source_names,kind,published):
    """text必须是真实Studio输出正文；published为当前构建发布资格的只读事实。"""
    published=published or {};commands=[]
    for line in text.splitlines():
        try:words=shlex.split(line)
        except ValueError:continue
        if len(words)==4 and words[-1]=="--object":commands.append(words)
    compiled=[row[1].replace("\\","/").rsplit("/",1)[-1].upper() for row in commands]
    missing=[];expected=[name.upper() for name in source_names]
    if sorted(compiled)!=sorted(expected):missing.append("actual independent TU command set")
    if text.count("SCCC OK SCO")!=len(expected):missing.append("real SCO success outputs")
    mode="--scl" if kind=="SCL" else "--dynamic"
    links=[]
    for line in text.splitlines():
        try:words=shlex.split(line)
        except ValueError:continue
        if len(words)==5 and words[2]==mode and words[4].startswith("@"):links.append(words)
    if len(links)!=1:missing.append("one real SCLD response-file command")
    if not any("SCLD OK " in line and "objects="+str(len(expected)) in line for line in text.splitlines()):missing.append("SCLD successful object count")
    if "SCCC ERROR" in text or "SCLD ERROR" in text:missing.append("compiler/linker error present")
    required=("success","current_epoch","success_epoch","build_generation","success_generation","all_jobs_reaped","publication_durable","output_revisions_current")
    if any(key not in published for key in required):missing.append("current build/publication observations")
    elif not (published["success"] and published["current_epoch"]==published["success_epoch"] and published["build_generation"]==published["success_generation"] and published["all_jobs_reaped"] and published["publication_durable"] and published["output_revisions_current"]):missing.append("current generation/epoch durable publication eligibility")
    return dict(verdict="UNVERIFIED" if missing else "OBSERVED_AND_CONSISTENT",missing=missing,commands=commands,links=links,
        output_text_sha256=sha(text.encode()),scope="BUILD_NOT_APPLICATION_EXECUTION")


def debug_step(before,after):
    required=("target","state","stop_generation","reason","eip","registers_valid","candidate_complete","pane_stop_generations")
    missing=[]
    if any(k not in value for value in (before,after) for k in required):return dict(verdict="UNVERIFIED",missing=["actual Debug2 published pause fields"])
    if before["target"]!=after["target"]:missing.append("same actual PID/generation/binding")
    if before["state"]!=3 or after["state"]!=3 or after["reason"]!=3 or before["stop_generation"]==after["stop_generation"]:missing.append("new true single-step pause")
    for value in (before,after):
        target=value["target"]
        if not isinstance(target,dict) or any(type(target.get(k)) is not int or target[k]<=0 for k in ("pid","generation","binding")):missing.append("non-placeholder actual target reference")
        if type(value["stop_generation"]) is not int or value["stop_generation"]<=0 or type(value["eip"]) is not int or value["eip"]<=0:missing.append("actual nonzero pause and EIP")
        if not value["registers_valid"] or not value["candidate_complete"] or not value["pane_stop_generations"] or any(v!=value["stop_generation"] for v in value["pane_stop_generations"]):missing.append("synchronized published panes")
    return dict(verdict="UNVERIFIED" if missing else "OBSERVED_AND_CONSISTENT",missing=missing,before=before,after=after,
        eip_changed=before["eip"]!=after["eip"],scope="REAL_TF_STEP_SAME_EIP_CAN_BE_A_SELF_JUMP")


def scl_module(observed,image):
    required=("kind","id","generation","base","image_bytes","flags","source_bytes","sha256","abi_major","abi_minor","library_version")
    if any(k not in observed for k in required):return dict(verdict="UNVERIFIED",missing=["actual runtime module properties"])
    # 此处只核现成正式SCL格式；不把app.map或文件名当运行期模块身份。
    from scl_format import validate
    parsed=validate(image);missing=[]
    if observed["kind"]!=2 or not observed["id"] or not observed["generation"] or not observed["base"]:missing.append("actual SCL load identity")
    if observed["flags"]&5!=5 or observed["flags"]&2:missing.append("verified and ready runtime registration")
    if observed["sha256"].lower()!=sha(image) or observed["source_bytes"]!=len(image) or observed["image_bytes"]!=parsed["image_size"]:missing.append("actual mapped image/file SHA identity")
    if any(observed[k]!=parsed[k] for k in ("abi_major","abi_minor","library_version")):missing.append("actual ABI/version")
    return dict(verdict="UNVERIFIED" if missing else "OBSERVED_AND_CONSISTENT",missing=missing,
        name=parsed["name"],image_sha256=sha(image),scope="ACTUAL_RUNTIME_REGISTRATION_PLUS_EXACT_FILE_NOT_STATIC_MAP")
