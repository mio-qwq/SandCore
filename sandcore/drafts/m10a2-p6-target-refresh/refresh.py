"""接Root现成VM的单目标真实refresh；导入/--plan不构建、不启动VM。"""
from pathlib import Path
import argparse
import copy
import importlib.util
import json
import os
import sys

sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
sys.path.insert(0,str(ROOT/"tools"))
_spec=importlib.util.spec_from_file_location("p6_refresh_contract",ROOT/"drafts/m10a2-p6-guest-generation/model.py")
contract=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(contract)
from scl_format import validate
from scd_format import validate_scx
from verify_m10a2_stdlib import source_contract

SCOPE="CURRENT_REFRESH_PREPARATION_ONLY_NOT_COMMON_GENERATION_ORIGIN"


def read(path):return contract.read_json(path)
def sha(path):return contract.sha(path)
def save(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")


def inside(path):
    path=Path(path).resolve(strict=True)
    if not path.is_relative_to(ROOT) or not path.is_file():raise ValueError("输入离开仓库: "+str(path))
    return path


def passed(report):
    if report.get("status")!="DECLARED_P6_NATIVE_GENERATION_PASS" or report.get("current_inputs_unchanged") is not True or report.get("source_disk_unchanged") is not True or report.get("changed_input_output_paths")!=[]:
        raise ValueError("原完整generation不是守恒PASS")


def preparation(previous,expected_sha,allowed=None):
    previous=inside(previous)
    if sha(previous)!=expected_sha:raise ValueError("原报告完整SHA不同")
    old=read(previous);passed(old)
    plan,payload=contract.source_plan()
    if set(old["objects"])!=set(plan["cases"]) or len(old["native_libraries"])!=8 or len(old["native_artifacts"])!=21:
        raise ValueError("必须继承完整8库/21artifact/27object，不削减集合")
    old_closures=old["preflight"]["source_closures"]
    changed=[label for label,current in plan["source_closures"].items() if old_closures.get(label)!=current]
    if allowed is not None and not set(changed)<=set(allowed):raise ValueError("实际变化超出Root明确范围: "+str(changed))
    if old["preflight"]["compiler_closure"]!=plan["compiler_closure"] or "SCLD" in changed:
        raise ValueError("编译器或最终链接器源变化，不能借旧工具来源假装当前工具")
    for label,obj in old["objects"].items():
        if label in changed:continue
        for source in obj["sources"]:
            key="user/"+source["guest"][8:] if source["guest"].startswith("SYS/SRC/") else old["preflight"]["public_header_sources"].get(source["guest"])
            if key is None or plan["source_input_sha256"].get(key)!=source["sha256"]:
                raise ValueError("未变标签仍引用不同原文，必须刷新: "+label+" "+source["path"])
    manifests={}
    for label in plan["cases"]:
        _,template,_=plan["cases"][label]
        file=ROOT/"user"/Path(template).with_suffix(".NATIVE.JSON")
        body=file.read_bytes();doc=contract.sdk.json_bytes(body)
        if label not in changed and contract.digest(body)!=old["preflight"]["manifests"][label]["sha256"]:
            raise ValueError("未变标签formal manifest变化: "+label)
        guest=contract.SOURCE_PREFIX+"/LINK/"+label+".NATIVE.JSON"
        payload[guest[1:]]=body
        manifests[label]=dict(path=guest,sha256=contract.digest(body),declared=doc,source=str(file))
    pins={previous:expected_sha}
    for name,value in plan["source_input_sha256"].items():pins[ROOT/name]=value
    for directory in (HERE,ROOT/"drafts/m10a2-p6-guest-generation"):
        for path in directory.glob("*.py"):pins[path]=sha(path)
    result=dict(schema=1,previous_report=str(previous),previous_report_sha256=expected_sha,changed_labels=changed,
        unchanged_labels=[key for key in plan["cases"] if key not in changed],**plan,manifests=manifests,
        compiler=old["native_artifacts"]["SCCC1.SCX"],linker=old["native_artifacts"]["SCLD.SCX"],
        scope="PLAN_ONLY_NOT_GENERATION_PASS")
    return result,payload,pins,old


def original(item,collection,name,previous,previous_sha,old):
    origin=item.get("generation_origin")
    if origin and origin["mode"]=="REUSED":
        path=inside(origin["report"])
        if sha(path)!=origin["report_sha256"]:raise ValueError("原始生成链报告SHA不同")
        report=read(path);passed(report)
        row=report[collection][name]
        if any(row[key]!=item[key] for key in ("sha256","bytes","compile_case")):raise ValueError("复用生成链字节不同")
        return path,origin["report_sha256"],report
    return previous,previous_sha,old


def copy_checked(source,target,expected=None,size=None):
    source=inside(source);body=source.read_bytes()
    if expected and contract.digest(body)!=expected or size is not None and len(body)!=size:raise ValueError("原产物字节不同: "+str(source))
    target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists():raise FileExistsError(target)
    target.write_bytes(body);return body


def recorded_sidecar(source,target,report,expected=None):
    source=inside(source)
    records=[row for row in report["cases"] if row.get("path") and Path(row["path"]).resolve()==source and row.get("sha256")]
    if expected is None:
        if len(records)!=1:raise ValueError("旧副文件缺唯一实际get记录: "+str(source))
        expected=records[0]["sha256"]
    size=records[0]["bytes"] if len(records)==1 else None
    body=copy_checked(source,target,expected,size)
    return dict(bytes=len(body),sha256=contract.digest(body),source_path=str(source))


def inherit(prepared,old,out):
    out=Path(out).resolve()
    if not out.is_relative_to(ROOT/"build") or not out.name.startswith("m10a2-p6-target-refresh") or out.exists():raise ValueError("需要本任务build前缀内新bundle")
    out.mkdir();previous=Path(prepared["previous_report"]);previous_sha=prepared["previous_report_sha256"]
    report=dict(status="REFRESH_PENDING_NOT_PASS",scope="P6_TARGET_NATIVE_REFRESH",cases=[],objects={},native_libraries={},native_artifacts={},
        source_data_sha256=old["source_data_sha256"],input_sha256=prepared["source_input_sha256"],source_input_sha256=prepared["source_input_sha256"],
        source_snapshot_scope=SCOPE,generated_libraries_installed=False,generated_applications_executed=False,full_M10a2=False,
        preflight=dict(old["preflight"],source_closures=prepared["source_closures"],compiler_closure=prepared["compiler_closure"],
            source_input_sha256=prepared["source_input_sha256"],manifests=prepared["manifests"]),
        source_reuse=dict(kind="P6_TARGET_REFRESH",previous_report=str(previous),previous_report_sha256=previous_sha,
            changed_labels=prepared["changed_labels"],unchanged_label_compile_closures={k:prepared["source_closures"][k] for k in prepared["unchanged_labels"]}))
    reused_case_keys=set()
    for collection,folder in (("native_libraries","native-libraries"),("native_artifacts","native-artifacts")):
        for name,item in old[collection].items():
            label=next((key for key,value in prepared["cases"].items() if old["preflight"]["manifests"][key]["declared"]["name"]==name and (collection=="native_libraries")==value[2]),None)
            if label in prepared["changed_labels"]:continue
            row=copy.deepcopy(item);source=inside(item["path"]);target=out/folder/name
            origin_path,origin_sha,origin_report=original(item,collection,name,previous,previous_sha,old)
            copy_checked(source,target,item["sha256"],item["bytes"])
            sidecars={".map":recorded_sidecar(str(source)+".map",Path(str(target)+".map"),old,item.get("map_sha256"))}
            if "debug" in item:copy_checked(str(source)+".sdbg",Path(str(target)+".sdbg"),item["debug"]["sha256"],item["debug"]["bytes"])
            elif Path(str(source)+".log").is_file():sidecars[".log"]=recorded_sidecar(str(source)+".log",Path(str(target)+".log"),old)
            origin=dict(mode="REUSED",report=str(origin_path),report_sha256=origin_sha,compile_case=item["compile_case"],
                source_input_sha256=origin_report["input_sha256"],include_manifest_closure=prepared["compiler_closure"] if name=="SCCC1.SCX" else prepared["source_closures"].get(label,prepared["source_closures"]["SCLD"]),
                compiler_sha256=item.get("compiler_sha256"),linker_sha256=item.get("linker_sha256"),generated_this_vm=False)
            row.update(path=str(target),generated_this_vm=False,installed_this_vm=False,executed_in_this_vm=False,generation_origin=origin,inherited_sidecars=sidecars)
            report[collection][name]=row
            for case_name in (item["compile_case"],item.get("object_compile_case")):
                if not case_name or (str(origin_path),case_name) in reused_case_keys:continue
                actual=[v for v in origin_report["cases"] if v["name"]==case_name]
                if len(actual)!=1 or actual[0].get("status")!="PASS" or actual[0].get("exit_code")!=0:raise ValueError("复用缺真实成功producer: "+name)
                case=copy.deepcopy(actual[0]);case.update(status="REUSED_PASS",original_status="PASS",execution_class="REUSED",executed_in_this_vm=False,
                    source_report=str(origin_path),source_report_sha256=origin_sha,source_case=case_name)
                report["cases"].append(case);reused_case_keys.add((str(origin_path),case_name))
    for label,item in old["objects"].items():
        if label in prepared["changed_labels"]:continue
        row=copy.deepcopy(item);source=inside(item["path"]);target=out/"objects"/(label+".SCO")
        copy_checked(source,target,item["sha256"],item["bytes"])
        sidecars={suffix:recorded_sidecar(str(source)+suffix,Path(str(target)+suffix),old) for suffix in (".log",".map")}
        row.update(path=str(target),generated_this_vm=False,generation_origin=dict(mode="REUSED",report=str(previous),report_sha256=previous_sha,
            compile_case=item["compile_case"],source_input_sha256=old["input_sha256"],include_manifest_closure=prepared["source_closures"][label],generated_this_vm=False),inherited_sidecars=sidecars)
        report["objects"][label]=row
    save(out/"p6-generation.json",report);return report


def write_provenance(report,out):
    generation={name:item["generation_origin"] for name,item in dict(report["native_libraries"],**report["native_artifacts"]).items()}
    value=dict(schema=1,guest_sccc_compiled=True,report="../p6-generation.json",qemu_pid=report.get("qemu_pid"),
        compiler_sha256=report["native_artifacts"]["SCCC1.SCX"]["sha256"],libraries=report["native_libraries"],applications=report["native_artifacts"],
        source_input_sha256=report["input_sha256"],source_snapshot_scope=SCOPE,artifact_generation=generation,source_reuse=report["source_reuse"],
        current_status=report["status"],generated_library_executed_this_vm=False,installed_library_origin="ROOT_EXISTING_PINNED_RUNTIME_NOT_REFRESH_OUTPUT")
    for folder in ("native-libraries","native-artifacts"):
        (out/folder).mkdir(exist_ok=True);save(out/folder/"provenance.json",value)


def sdk_candidate_audit(report,out,host_report,prepared):
    """直接复用严格SDK reader；Candidate的原文alias条件也逐行验证。"""
    sdk=contract.sdk;inputs=sdk.Inputs();_,recorded,_=sdk.verified_build(inside(host_report),inputs)
    proof=out/"native-libraries/provenance.json";libraries={}
    for label,(source,template,library) in prepared["cases"].items():
        if not library:continue
        name=prepared["manifests"][label]["declared"]["name"]
        roots=sdk.LIBRARIES.get(name,(None,("user/"+str(Path(source).parent)+"/",)))[1]
        headers=sdk.final_sdk.extra_headers(name) or sdk.library_headers(name)
        _,_,origin=sdk.actual_native(name,proof,prepared["manifests"][label]["declared"],inputs,roots,headers)
        libraries[name]=dict(origin_report=origin["origin_report"],origin_report_sha256=origin["origin_report_sha256"])
    _,compiler=sdk.compiler(proof,inputs,recorded)
    _,linker,_=sdk.final_sdk.native_linker(sdk,proof,inputs,recorded)
    for obj in report["objects"].values():
        for source in obj["sources"]:
            name="user/"+source["guest"][8:] if source["guest"].startswith("SYS/SRC/") else prepared["public_header_sources"].get(source["guest"])
            if name is None or report["source_input_sha256"].get(name)!=source["sha256"] or sha(ROOT/name)!=source["sha256"]:
                raise ValueError("Candidate源alias不等于真实SCDB原文: "+source["path"])
    if inputs.unchanged():raise ValueError("严格SDK读取时输入改变")
    return dict(actual_native=libraries,compiler=compiler,linker=linker,candidate_source_aliases_exact=True,checks_not_weakened=True)


def execute(harness,prepared,payload,pins,old,out,*,host_report,compiler="/SYS/BIN/SCCC.SCX",linker="/SYS/BIN/SCLD.SCX",work="/HOME/P6REFRESH"):
    """Root显式调用，复用其现有VM，不安装新库/应用，不复制磁盘。"""
    if not prepared["changed_labels"]:raise ValueError("闭包没有变化，不启动refresh工作")
    out=Path(out).resolve();report=inherit(prepared,old,out);report["qemu_pid"]=harness.vm.process.pid
    original_disk=inside(harness.vm.report["sources"]["sanddata"]["path"])
    if sha(original_disk)!=report["source_data_sha256"]:raise ValueError("Root VM的原只读盘不是同一来源")
    before,observed_before=harness.quiet();resources_before=harness.resources()
    def persist():save(out/"p6-generation.json",report)
    try:
        for key,guest in (("compiler",compiler),("linker",linker)):
            record=prepared[key];local=out/(key+"-actual-tool.SCX")
            body=harness.file(guest,"refresh-tool-"+key,local=local)
            if contract.digest(body)!=record["sha256"] or len(body)!=record["bytes"]:raise ValueError("VM实际native工具不同: "+key)
        changed_sources={name for label in prepared["changed_labels"] for name in prepared["source_closures"][label]
            if old["source_input_sha256"].get(name)!=prepared["source_input_sha256"][name]}
        staging=out/"upload";staging.mkdir()
        uploads={contract.SOURCE_PREFIX+"/"+name[5:]:contract.relative(name).read_bytes() for name in changed_sources}
        uploads.update({prepared["manifests"][label]["path"]:payload[prepared["manifests"][label]["path"][1:]] for label in prepared["changed_labels"]})
        harness.run("refresh-work-directories","mkdir -p "+work+"/OBJ "+work+"/OUT")
        for index,(target,body) in enumerate(uploads.items()):
            contract.guest_name(target);local=staging/(str(index)+".SRC");local.write_bytes(body)
            harness.run("refresh-source-parent-"+str(index),"mkdir -p "+target.rsplit("/",1)[0])
            transfer=harness.guest.client.put(local,target)
            if transfer["sha256"]!=contract.digest(body):raise ValueError("实际SRC上传摘要不同")
        report["tool_executions"]=[]
        for label in prepared["changed_labels"]:
            source,_,library=prepared["cases"][label];manifest=prepared["manifests"][label];declared=manifest["declared"]
            obj_guest=work+"/OBJ/"+label+".SCO";obj_local=out/"objects"/(label+".SCO");obj_local.parent.mkdir(exist_ok=True)
            command=compiler+" "+contract.SOURCE_PREFIX+"/"+source+" "+obj_guest+" --object"
            compiled=harness.run("refresh-"+label+"-actual-SCCC-object",command,contains=(b"SCCC OK SCO",),forbidden=(b"SCCC ERROR",),timeout=600)
            obj=harness.file(obj_guest,"refresh-"+label+"-SCO",local=obj_local)
            info=contract.object_contract(obj,payload,declared)
            for suffix in (".log",".map"):harness.file(obj_guest+suffix,"refresh-"+label+"-SCO"+suffix,local=Path(str(obj_local)+suffix))
            compiled=copy.deepcopy(compiled);compiled.update(generated_sha256=contract.digest(obj),generated_bytes=len(obj),compiler_sha256=prepared["compiler"]["sha256"],source_input_sha256=prepared["source_closures"][label],process_identity="NOT_OBSERVED",execution_class="FRESH")
            report["cases"].append(compiled)
            if not report["tool_executions"]:report["tool_executions"].append(dict(path=compiler,sha256=prepared["compiler"]["sha256"],case=compiled["name"],process_identity="NOT_OBSERVED"))
            target=work+"/OUT/"+declared["name"];local=out/("native-libraries" if library else "native-artifacts")/declared["name"];local.parent.mkdir(exist_ok=True)
            command=linker+" "+target+(" --scl " if library else " --dynamic ")+manifest["path"]+" "+obj_guest
            linked=harness.run("refresh-"+label+"-actual-native-SCLD-link",command,contains=(("SCLD OK "+declared["name"]).encode(),),forbidden=(b"SCLD ERROR",),timeout=600)
            body=harness.file(target,"refresh-"+label+"-image",local=local);tables=validate(body) if library else validate_scx(body)
            fmt=source_contract(tables,declared,not library)
            mapping=harness.file(target+".map","refresh-"+label+"-map",local=Path(str(local)+".map"))
            debug=harness.file(target+".sdbg","refresh-"+label+"-SCDB",local=Path(str(local)+".sdbg"))
            dbg=contract.debug_contract(debug,body,tables,payload,[info])
            linked=copy.deepcopy(linked);linked.update(generated_sha256=contract.digest(body),generated_bytes=len(body),compiler_sha256=prepared["compiler"]["sha256"],linker_sha256=prepared["linker"]["sha256"],source_input_sha256=prepared["source_closures"][label],process_identity="NOT_OBSERVED",execution_class="FRESH")
            report["cases"].append(linked)
            if not any(item["path"]==linker for item in report["tool_executions"]):report["tool_executions"].append(dict(path=linker,sha256=prepared["linker"]["sha256"],case=linked["name"],process_identity="NOT_OBSERVED"))
            origin=dict(mode="FRESH",report=str(out/"p6-generation.json"),compile_case=linked["name"],source_input_sha256=report["input_sha256"],include_manifest_closure=prepared["source_closures"][label],generated_this_vm=True)
            row=dict(path=str(local),sha256=contract.digest(body),bytes=len(body),compile_case=linked["name"],object_compile_case=compiled["name"],guest_target=target,
                guest_sccc_compiled=True,generated_by_actual_scld=True,compiler_sha256=prepared["compiler"]["sha256"],linker_sha256=prepared["linker"]["sha256"],manifest_sha256=manifest["sha256"],
                map_sha256=contract.digest(mapping),debug=dbg,generated_this_vm=True,installed_this_vm=False,executed_this_generated_file=False,format_contract=fmt,generation_origin=origin)
            report["native_libraries" if library else "native_artifacts"][declared["name"]]=row
            report["objects"][label]=dict(info,path=str(obj_local),compile_case=compiled["name"],compiler_sha256=prepared["compiler"]["sha256"],generated_this_vm=True,generation_origin=dict(origin,compile_case=compiled["name"]))
            persist()
        after,observed_after=harness.quiet();resources_after=harness.resources()
        ledger,okay=harness.ledger(before,after,resources_before,resources_after,False)
        owner=harness.append(dict(name="target-refresh-single-owner-account",**ledger,before_observations=observed_before,after_observations=observed_after),okay)
        report["cases"].append(copy.deepcopy(owner))
        changed=[str(path) for path,value in pins.items() if not path.is_file() or sha(path)!=value]
        report.update(changed_input_output_paths=changed,current_inputs_unchanged=not changed,source_disk_unchanged=sha(original_disk)==report["source_data_sha256"])
        if changed or not report["source_disk_unchanged"]:raise ValueError("refresh期间真实输入/原盘改变")
        if len(report["objects"])!=27 or len(report["native_libraries"])!=8 or len(report["native_artifacts"])!=21:raise ValueError("统一产物集合不完整")
        for name in ("SCCC1.SCX","SCLD.SCX"):report["native_artifacts"][name]["executed_this_vm"]=True;report["native_artifacts"][name]["executed_this_generated_file"]=True
        report["status"]="DECLARED_P6_NATIVE_GENERATION_PASS"
        persist();write_provenance(report,out)
        report["strict_reader_audit"]=sdk_candidate_audit(report,out,host_report,prepared)
    except BaseException as error:
        report.update(status="FAIL_OR_INTERRUPTED",error=dict(type=type(error).__name__,message=str(error)));raise
    finally:
        persist();write_provenance(report,out)
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--previous",type=Path,required=True);parser.add_argument("--previous-sha256",required=True)
    parser.add_argument("--allow-label",action="append");parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args()
    plan,_,_,_=preparation(args.previous,args.previous_sha256,args.allow_label)
    output=args.out.resolve()
    if not output.is_relative_to(ROOT/"build") or not output.name.startswith("m10a2-p6-target-refresh") or output.exists():raise ValueError("只写本任务build内新计划文件")
    save(output,plan);print("PLAN_ONLY",plan["changed_labels"],"NO_BUILD_NO_VM")


if __name__=="__main__":main()
