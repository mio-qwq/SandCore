#!/usr/bin/env python3
"""P6固定源码路径、单次Windows VM的真实原生生成；默认只做来源计划。"""
from pathlib import Path
import argparse
import json
import os
import re
import secrets
import struct
import time
import traceback
import model

ROOT, HERE = model.ROOT, model.HERE


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")


def task_identity(vm, symbols, layout, pid, expected_name):
    """复用现HMP只读观察；read屏障期间不会启动下一条producer。"""
    from m10_guest_boot import physical_word
    began=time.monotonic(); offsets=layout["values"]
    def word(address):
        if time.monotonic()-began>2:raise TimeoutError("PID只读暂停预算到达")
        return physical_word(vm,address)
    vm.qmp("stop")
    try:
        pool=symbols["records"]
        a=word(pool+(pid>>23)*4)
        b=word(a+((pid>>14)&511)*4) if a else 0
        block=word(b+((pid>>5)&511)*4) if b else 0
        if not block:raise ValueError("PID没有已提交记录")
        used,history=word(block),word(block+8)
        mask=1<<(pid&31)
        if not (used|history)&mask:raise ValueError("PID没有live或KEEP_IDLE身份")
        stride=word(pool+offsets["pool_stride"])
        if stride!=((offsets["record_size"]+15)&~15):raise ValueError("task_record实际步长不符")
        record=block+64+(pid&31)*stride
        generation=word(record+offsets["record_generation"])
        count=offsets["record_name_bytes"]
        if count!=12:raise ValueError("task name ABI改变")
        name=b"".join(struct.pack("<I",word(record+offsets["record_name"]+i*4)) for i in range(3)).split(b"\0",1)[0].decode("ascii")
        state=word(record+offsets["record_state"])
        if not generation or name.upper()!=expected_name[:11].upper():raise ValueError("PID已复用或不属于此工具")
        return dict(pid=pid,generation=generation,task_name=name,task_state=state,
            live=bool(used&mask),committed_history=bool(history&mask),
            method="READ_ONLY_PAUSED_TASK_RECORD_WITH_SHELL_PID_READ_BARRIER",guest_memory_writes=False,
            observed_before_shell_ack=True)
    finally:
        vm.qmp("cont")


def producer(harness, name, tool, arguments, tool_sha, closure, marker, layout, timeout=600):
    """不改compiler stdout，使用已有Guest的真实Shell/管理协议。"""
    from verify_m9 import process_cpu
    for value in [tool,*arguments]:model.guest_name(value) if value.startswith("/") else None
    if any(not re.fullmatch(r"[A-Za-z0-9_./-]+",value) for value in [tool,*arguments]):raise ValueError("producer参数不适合无歧义Shell")
    command=" ".join([tool,*arguments]); token="__P6_PID_"+secrets.token_hex(8)
    script=command+" & _p6_pid=$!; echo "+token+":$_p6_pid; read _p6_ack; wait $_p6_pid"
    item=dict(name=name,command=command,guest_shell_command=script,expected_exit=0,tool_path=tool,tool_sha256=tool_sha,
        source_input_sha256=closure,tool_executed_in_this_vm=True)
    harness.report["active_case"]=item;harness.persist()
    start=len(harness.guest.console);began=time.monotonic();cpu=process_cpu(harness.vm.process)
    try:
        pattern=rb"(?:\r?\n)"+token.encode()+rb":([0-9]+)\r?\n"
        done,start=harness.guest.dialog(script,pattern)
        matched=harness.guest.wait(pattern,start,timeout=timeout)
        pid=int(matched[1])
        if not 0<pid<=0x7fffffff:raise ValueError("Shell未给真实子PID")
        item["process_identity"]=task_identity(harness.vm,harness.symbols,layout,pid,Path(tool).name)
        seen=harness.report.setdefault("producer_pid_generations",{})
        if seen.get(str(pid))==item["process_identity"]["generation"]:
            raise ValueError("producer的PID/代数重复，不能把旧background身份当新spawn")
        seen[str(pid)]=item["process_identity"]["generation"]
        for artifact in harness.report["native_artifacts"].values():
            if artifact.get("guest_target")==tool:artifact["executed_this_generated_file"]=True
        if not any(value["path"]==tool for value in harness.report["generated_tools_executed"]):
            harness.report["generated_tools_executed"].append(dict(path=tool,sha256=tool_sha,
                scope="GENERATION_TOOL_ONLY_NOT_NEW_APPLICATION_EXECUTION",process_identity=item["process_identity"]))
        harness.persist()
        harness.guest.client.input(b"\n")
        code,output=harness.guest.finish_dialog(done,start,timeout=timeout)
        item["exit_code"]=code
    except BaseException as error:
        output=bytes(harness.guest.console[start:]);item.update(exit_code=None,error=repr(error))
        destination=harness.vm.out/("producer-%03d.stdout.bin"%len(harness.report["cases"]))
        destination.write_bytes(output)
        item.update(stdout_path=str(destination),stdout_sha256=model.digest(output),stdout_bytes=len(output),
            output=output.decode("utf-8","replace"),wall_seconds=time.monotonic()-began)
        harness.report["cases"].append(dict(item,status="FAIL_OR_INTERRUPTED"));harness.persist();raise
    destination=harness.vm.out/("producer-%03d.stdout.bin"%len(harness.report["cases"]))
    destination.write_bytes(output)
    item.update(stdout_path=str(destination),stdout_sha256=model.digest(output),stdout_bytes=len(output),
        output=output.decode("utf-8","replace"),wall_seconds=time.monotonic()-began,
        qemu_cpu_seconds=process_cpu(harness.vm.process)-cpu)
    return harness.append(item,code==0 and marker in output and b"SCCC ERROR" not in output and b"SCLD ERROR" not in output)


def execute(plan,payload,pins,out,accel):
    # Windows模块只在显式execute分支导入；source-plan/预检不会创建VM。
    import refresh_m10a2_ui_native as refresh
    import verify_m10a2_ui_platform as first
    import verify_m10a2_stdlib as stdlib
    from verify_m10a2_scl import prepare
    from scserial import QemuSession
    from verify_m9 import Guest
    from m10_guest_boot import unique_symbols,wait_first_desktop
    from scl_format import validate
    from scd_format import validate_scx
    build=Path(plan["build"]);data=Path(plan["data"]);symbols=unique_symbols(build/"core.sym")
    report=dict(status="RUNNING",scope="P6_NATIVE_GENERATION_ONLY",cases=[],screenshots=[],
        compiler=plan["old_g1"],native_libraries={},native_artifacts={},objects={},
        standard_library_behavior_scope=plan["standard_library_behavior_scope"],
        source_input_sha256=plan["source_input_sha256"],input_sha256=plan["source_input_sha256"],
        source_data_sha256=plan["source_data_sha256"],preflight=plan,
        generated_libraries_installed=False,generated_applications_executed=False,generated_tools_executed=[],full_M10a2=False)
    def persist():save(out/"p6-generation.json",report)
    first.BUILD,first.PACKAGE=build,model.PACKAGE
    old_tool=model.PACKAGE+"/TOOLS/ACTUALG1.SCX"
    new_tool=model.PACKAGE+"/TOOLS/SCCC1.SCX"
    seed=model.PACKAGE+"/BOOT/SCLD.SCX"
    with first.harness_configuration():
        with QemuSession(build/"sandcore.img",data,out,accel,True,audio=False,memory=256,
                data_prepare=lambda image:prepare(image,build,payload,runtime_fixtures=False)) as vm:
            report["qemu_pid"]=vm.process.pid
            for folder in ("native-libraries","native-artifacts","objects"):(out/folder).mkdir()
            guest=Guest(vm);harness=None;before=None;resources_before=None
            try:
                harness=refresh.RefreshHarness(vm,guest,report,symbols,persist,first.local_elf_symbols(build/"core.elf"))
                wait_first_desktop(guest,symbols,report);guest.connect();harness.warm()
                # 这两个变量属于持久父Shell，先建好再计整轮账，不能把它们
                # 的首次保留页误归编译器泄漏或事后用固定差额豁免。
                harness.run("warm-owned-parent-shell-producer-variables","_p6_pid=0; _p6_ack=ACK")
                before,observed_before=harness.quiet();resources_before=harness.resources()
                report["generation_owner_before"]=dict(pages=before,observations=observed_before,resources=resources_before);persist()
                item=producer(harness,"old-G1-current-source-SCCC",old_tool,[model.SOURCE_PREFIX+"/sccc.c",new_tool],
                    model.G1_SHA,plan["compiler_closure"],b"SCCC OK load=",plan["task_layout"])
                local=out/"native-artifacts/SCCC1.SCX"
                body=harness.file(new_tool,"current-SCCC-output",local=local);contract=model.legacy_scx(body)
                item.update(generated_sha256=model.digest(body),generated_bytes=len(body),compiler_sha256=model.G1_SHA,format_contract=contract)
                for suffix in (".log",".map"):harness.file(new_tool+suffix,"current-SCCC"+suffix,local=Path(str(local)+suffix))
                new_sha=model.digest(body)
                report["native_artifacts"]["SCCC1.SCX"]=dict(path=str(local),bytes=len(body),sha256=new_sha,compile_case=item["name"],
                    guest_target=new_tool,guest_sccc_compiled=True,executed_this_generated_file=False,chain_depth_after_HOST=2,format_contract=contract)
                persist()
                item=producer(harness,"current-SCCC-native-SCLD-seed",new_tool,[model.SOURCE_PREFIX+"/scld.c",seed,"--dynamic",model.SOURCE_PREFIX+"/SCLD.json"],
                    new_sha,plan["source_closures"]["SCLD"],b"SCCC OK SCX DYNAMIC",plan["task_layout"])
                seed_local=out/"native-artifacts/SCLD0.SCX"
                seed_body=harness.file(seed,"native-linker-seed-output",local=seed_local)
                seed_contract=stdlib.source_contract(validate_scx(seed_body),plan["native_seed_manifest"],True)
                seed_sha=model.digest(seed_body);item.update(generated_sha256=seed_sha,generated_bytes=len(seed_body),compiler_sha256=new_sha,format_contract=seed_contract)
                for suffix in (".log",".map"):harness.file(seed+suffix,"native-linker-seed"+suffix,local=Path(str(seed_local)+suffix))
                report["native_artifacts"]["SCCC1.SCX"]["executed_this_generated_file"]=True
                report["native_artifacts"]["SCLD0.SCX"]=dict(path=str(seed_local),bytes=len(seed_body),sha256=seed_sha,
                    compile_case=item["name"],guest_sccc_compiled=True,executed_this_generated_file=False,guest_target=seed,format_contract=seed_contract)
                persist()
                for label,(source,_,library) in plan["cases"].items():
                    manifest=plan["manifests"][label];declared=manifest["declared"]
                    obj_guest=model.PACKAGE+"/OBJ/"+label+".SCO";obj_local=out/"objects"/(label+".SCO")
                    compile_item=producer(harness,label+"-actual-SCCC-object",new_tool,[model.SOURCE_PREFIX+"/"+source,obj_guest,"--object"],
                        new_sha,plan["source_closures"][label],b"SCCC OK SCO",plan["task_layout"])
                    obj=harness.file(obj_guest,label+"-object",local=obj_local)
                    obj_info=model.object_contract(obj,payload,declared)
                    compile_item.update(generated_sha256=model.digest(obj),generated_bytes=len(obj),compiler_sha256=new_sha,object_contract=obj_info)
                    for suffix in (".log",".map"):harness.file(obj_guest+suffix,label+"-object"+suffix,local=Path(str(obj_local)+suffix))
                    report["objects"][label]=dict(obj_info,path=str(obj_local),compile_case=compile_item["name"],compiler_sha256=new_sha)
                    target=model.PACKAGE+"/OUT/"+declared["name"]
                    local=out/("native-libraries" if library else "native-artifacts")/declared["name"]
                    link_item=producer(harness,label+"-actual-native-SCLD-link",seed,[target,"--scl" if library else "--dynamic",manifest["path"],obj_guest],
                        seed_sha,plan["source_closures"][label],b"SCLD OK "+declared["name"].encode(),plan["task_layout"])
                    image=harness.file(target,label+"-linked-image",local=local)
                    tables=validate(image) if library else validate_scx(image)
                    contract=stdlib.source_contract(tables,declared,not library)
                    map_body=harness.file(target+".map",label+"-linked-map",local=Path(str(local)+".map"))
                    debug=harness.file(target+".sdbg",label+"-linked-SCDB",local=Path(str(local)+".sdbg"))
                    debug_info=model.debug_contract(debug,image,tables,payload,[obj_info])
                    if not map_body.startswith(b"SCDBMAP1MIO RVA\n" if library else b"SCDBMAP1MIO VA\n"):raise ValueError("最终map地址域不同")
                    link_item.update(generated_sha256=model.digest(image),generated_bytes=len(image),compiler_sha256=new_sha,
                        object_sha256=model.digest(obj),linker_sha256=seed_sha,manifest_sha256=manifest["sha256"],format_contract=contract,debug_contract=debug_info)
                    artifact=dict(path=str(local),sha256=model.digest(image),bytes=len(image),compile_case=link_item["name"],
                        object_compile_case=compile_item["name"],guest_target=target,guest_sccc_compiled=True,
                        generated_by_actual_scld=True,compiler_sha256=new_sha,linker_sha256=seed_sha,
                        manifest_sha256=manifest["sha256"],map_sha256=model.digest(map_body),debug=debug_info,
                        generated_this_vm=True,installed_this_vm=False,executed_this_generated_file=False,format_contract=contract)
                    report["native_libraries" if library else "native_artifacts"][declared["name"]]=artifact
                    report["native_artifacts"]["SCLD0.SCX"]["executed_this_generated_file"]=True
                    persist()
                after,observed_after=harness.quiet();resources_after=harness.resources()
                ledger,okay=harness.ledger(before,after,resources_before,resources_after,False)
                harness.append(dict(name="complete-generation-single-owner-page-account",**ledger,
                    before_observations=observed_before,after_observations=observed_after,resources_before=resources_before,resources_after=resources_after),okay)
                harness.screen("p6-generation-host-bootstrap-desktop")
                report["status"]="DECLARED_P6_NATIVE_GENERATION_PASS"
            except BaseException as error:
                report.update(status="FAIL_OR_INTERRUPTED",error=dict(message=str(error),traceback=traceback.format_exc()))
                report["failure_console_tail"]=bytes(guest.console[-12000:]).decode("utf-8","replace")
                if harness is not None:
                    try:harness.screen("p6-generation-failure")
                    except Exception as capture:report["screenshot_error"]=repr(capture)
                raise
            finally:
                try:guest.close()
                finally:
                    changed=pins.unchanged()
                    report.update(changed_input_output_paths=changed,current_inputs_unchanged=not changed,
                        source_disk_unchanged=str(data) not in changed)
                    if changed or not report["source_disk_unchanged"]:report["status"]="FAIL_OR_INTERRUPTED"
                    persist()
                    for folder in ("native-libraries","native-artifacts"):
                        save(out/folder/"provenance.json",dict(schema=1,guest_sccc_compiled=bool(report["native_artifacts"]),
                            report="../p6-generation.json",qemu_pid=report["qemu_pid"],libraries=report["native_libraries"],applications=report["native_artifacts"],
                            source_input_sha256=report["source_input_sha256"],compiler_sha256=report["native_artifacts"].get("SCCC1.SCX",{}).get("sha256"),
                            current_status=report["status"],generated_library_executed=False,installed_library_origin="PINNED_HOST_BOOTSTRAP",
                            producer_chain="ACTUAL_G1_TO_CURRENT_SCCC_TO_NATIVE_SCLD_TO_SCO_LINK"))
    if report["status"]!="DECLARED_P6_NATIVE_GENERATION_PASS":raise RuntimeError(report["status"])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config",type=Path)
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--execute",action="store_true",help="Root最后显式启动Windows QEMU；默认不启动")
    parser.add_argument("--source-plan",action="store_true",help="仅检查当前真实源闭包/SDK公开头，不需要未到达的最终pins")
    parser.add_argument("--accel",choices=("tcg","whpx"),default="tcg")
    args=parser.parse_args();out=args.out.resolve()
    if not out.is_relative_to(ROOT/"build") or not out.name.startswith("m10a2-p6-guest-generation") or out.exists():
        raise ValueError("需要本任务build前缀内新输出目录")
    if args.source_plan:
        if args.execute or args.config:parser.error("source-plan不能运行VM或冒充最终配置")
        plan,payload=model.source_plan();out.mkdir()
        save(out/"source-plan.json",dict(status="SOURCE_ONLY_NOT_FINAL_PREFLIGHT",**plan,payload_sha256={p:model.digest(b) for p,b in payload.items()},qemu_started=False))
        print("SOURCE_ONLY_NOT_FINAL_PREFLIGHT",len(plan["cases"]),len(plan["source_input_sha256"]));return
    if not args.config:parser.error("最终预检/执行需要Root给出的config")
    plan,payload,pins=model.preflight(args.config)
    if pins.unchanged():raise ValueError("预检期间输入发生变化")
    if not args.execute:
        out.mkdir();save(out/"preflight.json",dict(status="PINNED_PREFLIGHT_ONLY_NOT_GUEST_PASS",**plan,qemu_started=False));print("PINNED_PREFLIGHT_ONLY_NOT_GUEST_PASS");return
    if os.name!="nt":parser.error("只允许Windows QEMU")
    execute(plan,payload,pins,out,args.accel)


if __name__=="__main__":main()
