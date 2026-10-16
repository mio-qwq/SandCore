"""三个只读宿主检查：真实源闭包、i386布局、已有真实SCO/SCDB解析；不启动VM。"""
from pathlib import Path
import argparse
import ast
import json
import posixpath
import struct
import model


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--layout",type=Path,required=True)
    parser.add_argument("--p6-report",type=Path,default=model.ROOT/"build/m10a2-p6-sco-04/report.json")
    args=parser.parse_args();out=args.out.resolve()
    if not out.is_relative_to(model.ROOT/"build") or not out.name.startswith("m10a2-p6-guest-generation") or out.exists():raise ValueError("需要本任务build内新输出目录")
    out.mkdir();checks=[]
    for path in model.HERE.glob("*.py"):ast.parse(path.read_text(encoding="utf-8"),filename=str(path))
    plan,payload=model.source_plan()
    assert len(plan["cases"])==27 and len([value for value in plan["cases"].values() if value[2]])==8 and plan["max_prepared_source_path_bytes"]<=63
    assert all(not any(word in Path(name).name.upper() for word in model.sdk.FORBIDDEN) for name in payload if name.startswith("SYS/INC/"))
    checks.append(dict(name="actual-source-closure-and-SDK-whitelist",status="PASS",cases=len(plan["cases"]),
        sources=len(plan["source_input_sha256"]),public_headers=len(plan["public_header_sources"]),max_prepared_path_bytes=plan["max_prepared_source_path_bytes"],
        source_input_sha256=plan["source_input_sha256"]))
    layout=model.read_json(args.layout);values=layout["values"]
    assert values["record_generation"]==168 and values["record_name_bytes"]==12
    assert all(model.sha(model.relative(name))==value for name,value in layout["headers"].items())
    checks.append(dict(name="existing-i386-layout-extractor-current-headers",status="PASS",layout_sha256=model.sha(args.layout),
        fields={name:values[name] for name in ("record_size","record_generation","record_name","record_name_bytes","record_state","pool_stride")}))
    report=model.read_json(args.p6_report)
    item=next(item for item in report["cases"] if item["status"]=="PASS")
    obj_path=args.p6_report.parent/(item["name"]+".SCO");obj=obj_path.read_bytes()
    assert model.digest(obj)==item["object_sha256"]
    files,count=struct.unpack_from("<2I",obj,52);strings,size=struct.unpack_from("<2I",obj,68)
    text=model.strings_at(obj,strings,size);actual_payload={}
    for i in range(count):
        name=text(struct.unpack_from("<I",obj,files+i*16)[0]);normalized=posixpath.normpath("/"+name.lstrip("/"))[1:]
        if normalized.startswith("SYS/INC/"):
            base=normalized[8:];source=next(path for path in [model.ROOT/"user"/base,model.ROOT/"user/stdlib"/base,model.ROOT/"user/sclib"/base,model.ROOT/"user/runtime"/base] if path.is_file())
        else:source=model.ROOT/normalized
        actual_payload[normalized]=source.read_bytes()
    manifest_path=args.p6_report.parent/(item["name"]+".NATIVE.JSON");manifest=model.read_json(manifest_path)
    info=model.object_contract(obj,actual_payload,manifest,prefixes=("user/","SYS/INC/"))
    image_path=model.relative(item["image_path"]);image=image_path.read_bytes();debug_path=Path(str(image_path)+".sdbg")
    assert model.digest(image)==item["image_sha256"]
    tables=model.scl_validate(image) if image[:8]==b"SCL1MIO\0" else model.validate_scx(image)
    debug=model.debug_contract(debug_path.read_bytes(),image,tables,actual_payload,[info],prefixes=("user/","SYS/INC/"))
    checks.append(dict(name="existing-real-SCO-SCLD-SCDB-source-and-layout-parser",status="PASS",scope="HOST_IO_EXISTING_ARTIFACTS_ONLY_NOT_GUEST",
        origin_report=str(args.p6_report),origin_sha256=model.sha(args.p6_report),case=item["name"],
        object_sha256=model.digest(obj),image_sha256=model.digest(image),debug=debug))
    result=dict(status="SOURCE_AND_READ_ONLY_HOST_CHECKS_PASS",qemu_started=False,guest_generation_run=False,
        final_runtime_pins_supplied=False,checks=checks,files={p.name:model.sha(p) for p in model.HERE.glob("*.py")})
    (out/"report.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(result["status"],len(checks),"QEMU_NOT_STARTED")


if __name__=="__main__":main()
