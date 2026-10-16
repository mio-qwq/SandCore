"""P6只读计划/来源与文件表校验；此模块不导入QEMU或Windows串口。"""
from __future__ import annotations
from pathlib import Path, PurePosixPath
import ast
import hashlib
import importlib.util
import json
import posixpath
import re
import struct
import sys
sys.dont_write_bytecode=True

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import stage_m10a2_sdk as sdk
from scl_format import validate as scl_validate
from scd_format import validate_scx

SOURCE_PREFIX = "/SYS/SRC"
PACKAGE = "/HOME/M10P6"
G1_REPORT = "build/m10a2-ui-platform-guest-05/ui-platform.json"
G1_REPORT_SHA = "5535d2fdb7c6736aa7321d250bf521564055253b319199b6bf85d7f594c7e993"
G1_SHA = "dd7ba9d45fbd1abbafa3f5468dc2d9ee5822625aca052df2e6655945aa302138"
HELPERS = ("refresh_m10a2_ui_native.py", "verify_m10a2_shell_generation.py", "verify_m10a2_ui_platform.py",
    "verify_m10a2_stdlib.py", "verify_m10a2_scl.py", "verify_m9.py", "scserial.py", "serial_protocol.py",
    "m10_guest_boot.py", "m10a2_shell_inspect.py", "scl_format.py", "scd_format.py", "stage_m10a2_sdk.py",
    "check_m10a2_p6_objects.py")


def digest(body): return hashlib.sha256(body).hexdigest()
def sha(path):
    value=hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda:stream.read(1<<20),b""): value.update(block)
    return value.hexdigest()
def read_json(path): return sdk.json_bytes(Path(path).read_bytes())


def relative(name):
    if not isinstance(name, str) or "\\" in name or PurePosixPath(name).is_absolute() or ":" in name:
        raise ValueError("需要仓库相对路径: " + repr(name))
    if any(p in ("", ".", "..") for p in name.split("/")):
        raise ValueError("输入路径穿越/别名: " + name)
    path = (ROOT / name).resolve(strict=True)
    if not path.is_relative_to(ROOT) or not path.is_file():
        raise ValueError("输入不是仓库内真实文件: " + name)
    return path


def guest_name(name):
    if not isinstance(name, str) or not name.startswith("/") or "\\" in name or ":" in name:
        raise ValueError("客体需要固定绝对路径: " + repr(name))
    if any(p in ("", ".", "..") for p in name[1:].split("/")) or not 1 <= len(name.encode()) <= 63:
        raise ValueError("客体路径不是规范1..63B: " + name)
    if not re.fullmatch(r"/[A-Za-z0-9_./-]+", name):
        raise ValueError("客体路径不适合精确Shell参数: " + name)
    return name


def cases():
    # CASES源于主代理同一张实际对象表；不复制一份会漂移的组件列表。
    tree = ast.parse(relative("tools/check_m10a2_p6_objects.py").read_text(encoding="utf-8"))
    table = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
                 and any(isinstance(t, ast.Name) and t.id == "CASES" for t in n.targets))
    # Root确认旧stdlib provenance的SC32API.H已变化；两库也只重生成一次，
    # 不把旧接口SHA伪写成当前头，也不重跑P3功能矩阵。来源仍取SDK同源表。
    for name in ("SCCLIB","SCLIB"):
        manifest,_=sdk.LIBRARIES[name+".SCL"]
        value=(str(PurePosixPath(manifest).with_suffix(".C"))[5:],manifest[5:],True)
        if name in table and table[name]!=value:raise ValueError("SDK与P6基础库源不同: "+name)
        table.setdefault(name,value)
    required = {"SCLDR", "SC32API", "SCCLIB", "SCLIB", "SCUI", "SCSHELL", "SCGFX", "SCTERM", "SCLD", "SESSION2", "LOGIN2", "SOUND2", "SOUNDWORK", "SCFILEWORK", "DESKTOP"}
    if not required <= set(table) or any(not re.fullmatch(r"[A-Z0-9_]+", key) for key in table):
        raise ValueError("当前P6 CASES缺必要生成项或标签非法")
    return table


def closure(seeds, compiler_cli=False):
    roots = [ROOT / "user" / p for p in ("", "stdlib", "sclib", "runtime", "textdata")]
    pending, seen = [relative(name) for name in seeds], {}
    include = re.compile(r'(?m)^[ \t]*#[ \t]*include[ \t]*(?:"([^"\r\n]+)"|<([^>\r\n]+)>)[ \t]*$')
    while pending:
        path = pending.pop().resolve(strict=True)
        if not path.is_relative_to(ROOT / "user"):
            raise ValueError("源include越出user: " + str(path))
        key = path.relative_to(ROOT).as_posix()
        if key in seen: continue
        seen[key] = sha(path)
        if path.suffix.lower() not in (".c", ".h", ".inc"): continue
        text = path.read_text(encoding="utf-8").replace("\\\n", "").replace("\\\r\n", "")
        text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
        if compiler_cli and key == "user/sccc.c" and not re.search(r"(?m)^#define\s+SCCC_CLI\s+1\s*$", text):
            raise ValueError("原生编译器入口不再固定CLI")
        if compiler_cli and key == "user/s3c.c":
            branch = re.compile(r'#ifdef\s+SCCC_CLI\s*#include\s+"COMPILECLI.inc"\s*#else\s*#include\s+"COMPILEUI.inc"\s*#endif')
            if len(branch.findall(text)) != 1: raise ValueError("SCCC CLI分支结构变动")
            text = branch.sub('#include "COMPILECLI.inc"\n', text)
        parsed = list(include.finditer(text))
        if len(parsed) != len(re.findall(r"(?m)^\s*#\s*include\b[^\n]*", text)):
            raise ValueError("无法闭合非literal include: " + key)
        for item in parsed:
            name = item.group(1) or item.group(2)
            target = next((p / name for p in [path.parent] + roots if (p / name).is_file()), None)
            if target is None: raise ValueError("缺实际include: " + key + " -> " + name)
            pending.append(target)
    return dict(sorted(seen.items()))


def public_headers(plugin_paths=()):
    plan, origins = {}, {}
    entries = list(sdk.HEADERS.items()) + [(name, "user/stdlib/" + name) for name in sdk.STANDARD]
    for file in plugin_paths:
        doc = read_json(file)
        if doc.get("schema") != 1 or not isinstance(doc.get("public_headers"), list):
            raise ValueError("SDK插件缺正式public_headers: " + str(file))
        entries += [(Path(name).name, name) for name in doc["public_headers"]]
    for target, source in entries:
        path = relative(source); target = "SYS/INC/" + target
        sdk.header_allowed(path, target)
        guest_name("/" + target)
        if target.upper() in {k.upper() for k in plan}: raise ValueError("INC身份重复: " + target)
        plan[target] = dict(body=path.read_bytes()); origins[target] = source
    sdk.public_include_graph(plan)
    return {key: value["body"] for key, value in plan.items()}, origins


def source_plan(plugin_paths=()):
    table, compiler = cases(), closure(["user/sccc.c"], True)
    per_case={}
    for key,(source,manifest,_) in table.items():
        seeds=["user/"+source,"user/"+manifest]
        native="user/"+str(PurePosixPath(manifest).with_suffix(".NATIVE.JSON"))
        if (ROOT/native).is_file():seeds.append(native)
        per_case[key]=closure(seeds)
    all_sources = dict(compiler)
    for values in per_case.values(): all_sources.update(values)
    headers, header_sources = public_headers(plugin_paths)
    all_sources.update({name: sha(relative(name)) for name in header_sources.values()})
    payload={}
    for name,expected in all_sources.items():
        body=relative(name).read_bytes()
        if digest(body)!=expected:raise ValueError("计划读取时源发生变化: "+name)
        payload[guest_name(SOURCE_PREFIX+"/"+name[5:])[1:]]=body
    payload.update(headers)
    for target,source in header_sources.items():
        if digest(payload[target])!=all_sources[source]:raise ValueError("INC源在读取期间变化: "+source)
    return dict(cases=table, compiler_closure=compiler, source_closures=per_case,
        source_input_sha256=all_sources, public_header_sources=header_sources,
        max_prepared_source_path_bytes=max(len(("/"+p).encode()) for p in payload if p.startswith("SYS/SRC/"))), payload


class Pins:
    def __init__(self): self.values = {}
    def file(self, spec):
        if set(spec) != {"path", "sha256", "bytes"}: raise ValueError("pin需要path/sha256/bytes")
        path = relative(spec["path"])
        if sha(path) != spec["sha256"] or path.stat().st_size != spec["bytes"]: raise ValueError("pin不同: " + str(path))
        self.values[path] = spec["sha256"]; return path
    def read(self, spec):
        path=self.file(spec); body=path.read_bytes()
        if digest(body)!=spec["sha256"]:raise ValueError("读取时改变: "+str(path))
        return path,body
    def unchanged(self): return [str(p) for p, value in self.values.items() if not p.is_file() or sha(p) != value]


def old_g1(pins):
    path = relative(G1_REPORT); raw = path.read_bytes()
    if digest(raw) != G1_REPORT_SHA: raise ValueError("旧G1原报告完整SHA不同")
    pins.values[path] = G1_REPORT_SHA; report = sdk.json_bytes(raw)
    if report.get("status") != "DECLARED_PLATFORM_TEXT_G1_FIRST_ROUND_PASS" or not report.get("current_inputs_unchanged") or not report.get("source_disk_unchanged"):
        raise ValueError("旧G1来源未成立")
    item = report["native_artifacts"]["SCCC1.SCX"]
    actual = Path(item["path"]).resolve(strict=True)
    if not actual.is_relative_to(path.parent): raise ValueError("G1产物离开原报告目录")
    body = actual.read_bytes()
    if item["sha256"] != G1_SHA or digest(body) != G1_SHA or len(body) != item["bytes"]: raise ValueError("G1实际字节不同")
    pins.values[actual] = G1_SHA
    case = [c for c in report["cases"] if c["name"] == item["compile_case"]]
    if len(case) != 1 or case[0].get("status") != "PASS" or case[0].get("exit_code") != 0 or case[0].get("generated_sha256") != G1_SHA or case[0].get("generated_bytes")!=len(body) or item.get("guest_sccc_compiled") is not True or item.get("executed_this_generated_file") is not True:
        raise ValueError("旧G1缺真实成功producer")
    stdout = Path(case[0]["stdout_path"]).resolve(strict=True)
    if not stdout.is_relative_to(path.parent) or sha(stdout) != case[0]["stdout_sha256"] or stdout.stat().st_size != case[0]["stdout_bytes"]:
        raise ValueError("G1实际编译stdout不同")
    pins.values[stdout] = sha(stdout)
    return body, dict(path=str(actual), sha256=G1_SHA, bytes=len(body), report=str(path), report_sha256=G1_REPORT_SHA,
        producer_case=case[0]["name"], format_contract=legacy_scx(body), prior_generated_and_executed=True)


def legacy_scx(body):
    if len(body) < 36 or body[:8] != b"SCX1MIO\0": raise ValueError("不是旧SCX")
    entry, load, bss, stack, flags, base, author = struct.unpack_from("<7I", body, 8)
    if not (0 < load <= 262108 and entry < load and load+bss <= 0x3d0000 and len(body) == load+36
            and stack == 131072 and not flags and base == 0x400000 and author == 0x004f494d):
        raise ValueError("旧SCX load/BSS/ABI边界不同")
    return dict(load_bytes=load, bss_bytes=bss, stack_bytes=stack, exact_file_bytes=len(body))


def preflight(config_path):
    config_path = Path(config_path).resolve(strict=True); config = read_json(config_path)
    fields = {"schema", "build", "build_report", "p6_report", "data", "source_sha256", "bootstrap_files", "sdk_plugins", "task_layout"}
    if set(config) != fields or config["schema"] != 1: raise ValueError("配置字段/schema不同，见README")
    pins = Pins(); pins.values[config_path] = sha(config_path)
    build = (ROOT / config["build"]).resolve(strict=True)
    if not build.is_relative_to(ROOT / "build") or not build.is_dir(): raise ValueError("HOST构建必须在build内")
    proof_path, proof_raw = pins.read(config["build_report"]); proof = sdk.json_bytes(proof_raw)
    if proof.get("exit_code") != 0 or any(proof.get(k) not in (None, []) for k in ("changed_source_files", "source_files_modified_during_build")):
        raise ValueError("需要成功且源码保持的HOST构建报告")
    source_records=proof.get("inputs_sha256") or {item["path"]:item["sha256"] for item in proof.get("inputs",[])}
    plugins = [pins.read(item)[0] for item in config["sdk_plugins"]]
    plan, payload = source_plan(plugins)
    for name, value in plan["source_input_sha256"].items():
        if config["source_sha256"].get(name) != value: raise ValueError("本次源闭包未绑定最终build: " + name)
    for name, value in config["source_sha256"].items():
        path = relative(name)
        if not re.fullmatch(r"[0-9a-f]{64}", value) or sha(path) != value or source_records.get(name)!=value:
            raise ValueError("最终源码pin与当前/最终build不同: " + name)
        pins.values[path] = value
    required_core = ("sandcore.img", "core.sym", "core.elf", "core.bin", "fs/SYS/CORE/CORE.SKM")
    output_records = {item["path"]: item for item in proof.get("outputs", [])}
    runtime = {}
    for name in required_core:
        path = build / name; key = path.relative_to(ROOT).as_posix()
        if key not in output_records: raise ValueError("构建报告缺runtime输出: " + key)
        item = output_records[key]; pins.read({k:item[k] for k in ("path", "sha256", "bytes")}); runtime[name] = item["sha256"]
    core=(build/"fs/SYS/CORE/CORE.SKM").read_bytes()
    from mkfs_m10 import check_main
    from mkcore import inspect_elf
    check_main(core);elf=inspect_elf(build/"core.elf")
    if core[128:] != (build/"core.bin").read_bytes(): raise ValueError("CORE与符号载荷不是同批")
    base,entry,image,bss_offset,bss=struct.unpack_from("<5I",core,36)
    if (elf["_core_start"],elf["__core_image_end"],elf["__bss_start"],elf["__bss_end"])!=(base+entry,base+image,base+bss_offset,base+bss_offset+bss):
        raise ValueError("CORE和ELF真实映像/BSS边界不同")
    rows={};origins={};p6_proofs=[]
    specs=config["p6_report"] if isinstance(config["p6_report"],list) else [config["p6_report"]]
    for spec in specs:
        p6_path,p6_raw=pins.read(spec);p6=sdk.json_bytes(p6_raw)
        if p6.get("scope")!="P6_REAL_SCCC_SCO_SCLD_WITH_HOST_IO":raise ValueError("不是实际宿主SCO来源")
        p6_proofs.append(dict(path=str(p6_path),sha256=sha(p6_path),original_status=p6.get("status")))
        for row in p6.get("cases",[]):
            key=row["name"];inputs=row.get("actual_compiler_input_sha256",{})
            if key not in plan["cases"] or row.get("status")!="PASS" or not inputs:continue
            if any(plan["source_input_sha256"].get(name)!=value for name,value in inputs.items()):continue
            promoted=relative("user/"+str(PurePosixPath(plan["cases"][key][1]).with_suffix(".NATIVE.JSON")))
            if row["manifest_sha256"]!=sha(promoted):continue
            if key in rows and (row["manifest_sha256"],row["object_sha256"])!=(rows[key]["manifest_sha256"],rows[key]["object_sha256"]):
                raise ValueError("同源成功HOST对象来源冲突: "+key)
            rows[key]=row;origins[key]=p6_path
    if set(rows)!=set(plan["cases"]):raise ValueError("缺当前源成功SCO来源: "+str(sorted(set(plan["cases"])-set(rows))))
    manifests = {}
    for key, (source, declared, library) in plan["cases"].items():
        row = rows[key];p6_path=origins[key]
        if row.get("status") != "PASS" or row.get("compile_exit") != 0 or row.get("link_exit") != 0: raise ValueError("P6 case未通过: " + key)
        manifest=relative("user/"+str(PurePosixPath(declared).with_suffix(".NATIVE.JSON")))
        original_manifest=p6_path.parent/(key+".NATIVE.JSON")
        if not original_manifest.is_file() or sha(original_manifest)!=row["manifest_sha256"] or sha(manifest)!=row["manifest_sha256"]:
            raise ValueError("promote正式NATIVE.JSON与原来源不同: "+key)
        pins.values[original_manifest]=row["manifest_sha256"]
        pins.values[manifest] = sha(manifest); body = manifest.read_bytes(); doc = sdk.json_bytes(body)
        original = read_json(relative("user/"+declared))
        if {k:v for k,v in doc.items() if k not in ("imports", "dependencies")} != {k:v for k,v in original.items() if k not in ("imports", "dependencies")}:
            raise ValueError("NATIVE清单改变正式身份/导出: " + key)
        for name, value in row["actual_compiler_input_sha256"].items():
            if plan["source_input_sha256"].get(name) != value: raise ValueError("HOST对象不是当前闭包: " + key + " " + name)
        host_object=p6_path.parent/(key+".SCO")
        if not host_object.is_file() or sha(host_object)!=row["object_sha256"]:raise ValueError("HOST对象真实字节不同: "+key)
        pins.values[host_object]=row["object_sha256"];obj=host_object.read_bytes()
        files,count=struct.unpack_from("<2I",obj,52);strings,nstrings=struct.unpack_from("<2I",obj,68)
        text=strings_at(obj,strings,nstrings);max_source_path=0
        for i in range(count):
            offset,nbytes,fnv,reserved=struct.unpack_from("<4I",obj,files+i*16);raw=text(offset).lstrip("/")
            projected=SOURCE_PREFIX+"/"+raw[5:] if raw.startswith("user/") else "/"+raw
            if len(projected.encode())>63:raise ValueError("实际PP源路径换固定前缀后超过63B: "+projected)
            source_record(projected,nbytes,fnv,payload)
            max_source_path=max(max_source_path,len(projected.encode()))
        guest = guest_name(SOURCE_PREFIX+"/LINK/"+key+".NATIVE.JSON")
        payload[guest[1:]] = body; manifests[key] = dict(path=guest, sha256=digest(body), declared=doc,
            origin_report=str(p6_path),origin_report_sha256=sha(p6_path),host_object_sha256=row["object_sha256"],max_actual_projected_source_path_bytes=max_source_path)
    # 单源seed严格用原SCLD.json；其符号集合必须已被实际SCO交叉核验。
    seed = read_json(relative("user/SCLD.json"))
    if {i["name"] for i in seed["imports"]} != {i["name"] for i in manifests["SCLD"]["declared"]["imports"]}:
        raise ValueError("原SCLD.json与真实native对象imports不同")
    bootstrap = {}
    for item in config["bootstrap_files"]:
        if set(item) != {"guest", "artifact", "origin"}: raise ValueError("bootstrap字段不同")
        target = guest_name(item["guest"])[1:]; path, body = pins.read(item["artifact"])
        recorded=output_records.get(path.relative_to(ROOT).as_posix())
        if item["origin"].startswith("HOST") and (not recorded or recorded["sha256"]!=digest(body) or recorded["bytes"]!=len(body)):
            raise ValueError("HOST bootstrap不是最终build的真实输出: "+target)
        if target.upper().startswith(("SYS/INC/", "SYS/SRC/")) or target.upper() in {k.upper() for k in payload}:
            raise ValueError("bootstrap不能覆盖SRC/INC: " + target)
        if target.upper() in {k.upper() for k in bootstrap}: raise ValueError("重复bootstrap: " + target)
        if item["origin"] not in ("HOST_BOOTSTRAP", "HOST_CODEC_EXCEPTION", "RESOURCE"): raise ValueError("bootstrap来源必须明确")
        if target.endswith(".SCL"):
            scl_validate(body)
            if target == "SYS/LIB/SCSOUND.SCL" and item["origin"] != "HOST_CODEC_EXCEPTION": raise ValueError("SCSOUND必须明确HOST例外")
        bootstrap[target] = dict(path=str(path), bytes=len(body), sha256=digest(body), origin=item["origin"]); payload[target] = body
    for name in ("SCLDR", "SC32API", "SCCLIB", "SCLIB", "SCUI", "SCSHELL", "SCGFX", "SCTERM", "SCSOUND"):
        if "SYS/LIB/"+name+".SCL" not in bootstrap: raise ValueError("缺HOST执行库: " + name)
    for name in ("SYS/CORE/LOGIN2.SCX", "SYS/SHELL/SCSHELL.SCX"):
        if bootstrap.get(name, {}).get("origin") != "HOST_BOOTSTRAP": raise ValueError("缺CORE精确发行的HOST登录/桌面: " + name)
        validate_scx(payload[name])
    libraries={name.rsplit("/",1)[1]:dict(table=scl_validate(body)) for name,body in payload.items() if name.startswith("SYS/LIB/") and name.endswith(".SCL")}
    bootstrap_order=sdk.closure(libraries)
    data_path = pins.file(config["data"])
    # 只读取原盘hash；不会在plan阶段复制/修改数据盘。
    old, old_origin = old_g1(pins); payload[PACKAGE[1:]+"/TOOLS/ACTUALG1.SCX"] = old
    for folder in ("HOST", "BOOT", "OUT", "OBJ"):
        payload[PACKAGE[1:]+"/"+folder+"/.KEEP"] = b""
    payload[PACKAGE[1:]+"/WARM.DAT"] = b""
    layout_path, layout_raw = pins.read(config["task_layout"]); layout = sdk.json_bytes(layout_raw)
    needed = {"record_size", "record_generation", "record_name", "record_name_bytes", "record_state", "pool_stride"}
    if not needed <= set(layout.get("values", {})): raise ValueError("task layout缺真实字段")
    for name, value in layout["headers"].items():
        if config["source_sha256"].get(name) != value: raise ValueError("layout与本批构建头不同: " + name)
    for name in ("kernel/objpool.c", "kernel/task.c", "kernel/desktop.c", "kernel/ttf.c"):
        if name not in config["source_sha256"]: raise ValueError("只读账布局缺本批实现pin: " + name)
    # issuer必须来自这份成功CORE构建，并精确绑定本VM装入的HOST字节。
    issued={}
    all_records=dict(source_records)
    all_records.update({name:item["sha256"] for name,item in output_records.items()})
    for basename in ("login_program_issue.h","login_resource_issue.h","shell_host_issue.h"):
        matches=[name for name in all_records if Path(name).name==basename]
        if len(matches)!=1:raise ValueError("CORE构建缺唯一issuer头记录: "+basename)
        # 报告中的真实生成头还须匹配CORE中真正编入的摘要和资源表；
        # 双方共同绑定本VM字节，不能只相信另一个同名头文件。
        path=relative(matches[0])
        text=path.read_text(encoding="ascii")
        if sha(path)!=all_records[matches[0]]:raise ValueError("issuer头改变: "+basename)
        pins.values[path]=sha(path); issued[basename]=text
    def issued_digest(text,label,artifact):
        size=re.search(r"\b"+label+r"_bytes\s*=\s*(\d+)u;",text)
        value=re.search(r"\b"+label+r"_sha256\[32\]\s*=\s*\{([^}]+)\}",text)
        if not size or not value:raise ValueError("issuer字段不同: "+label)
        body=bytes(int(v,16) for v in re.findall(r"0x([0-9a-fA-F]{2})",value[1]))
        if int(size[1])!=artifact["bytes"] or body.hex()!=artifact["sha256"]:raise ValueError("issuer不绑定HOST字节: "+label)
    issued_digest(issued["login_program_issue.h"],"login_issued",bootstrap["SYS/CORE/LOGIN2.SCX"])
    issued_digest(issued["shell_host_issue.h"],"shell_host_issued",bootstrap["SYS/SHELL/SCSHELL.SCX"])
    resources=re.findall(r'\{"([^"]+)",(\d+)u,\{([^}]+)\}\}',issued["login_resource_issue.h"])
    required={"SYS/LIB/"+n+".SCL" for n in ("SCLDR","SC32API","SCCLIB","SCLIB","SCUI")}|{"SYS/UI/UAX15_1.SCU"}
    if len(resources)!=6 or {row[0] for row in resources}!=required:raise ValueError("LOGIN资源issuer范围不同")
    for name,size,value in resources:
        body=payload.get(name); expected=bytes(int(v,16) for v in re.findall(r"0x([0-9a-fA-F]{2})",value)).hex()
        if body is None or len(body)!=int(size) or digest(body)!=expected:raise ValueError("LOGIN资源issuer不同: "+name)
    from m10_guest_boot import unique_symbols
    symbols=unique_symbols(build/"core.sym")
    def core_bytes(address,count):
        offset=address-base
        if offset<0 or count>len(core)-128-offset:raise ValueError("CORE issuer字节超出实际载荷")
        return core[128+offset:128+offset+count]
    for symbol,target in (("login_issued_sha256","SYS/CORE/LOGIN2.SCX"),("shell_host_issued_sha256","SYS/SHELL/SCSHELL.SCX")):
        if symbol not in symbols or core_bytes(symbols[symbol],32).hex()!=bootstrap[target]["sha256"]:raise ValueError("CORE内实际发行摘要不同: "+symbol)
    if "login_issued_resources" not in symbols:raise ValueError("CORE缺真实login资源表符号")
    compiled_resources=set()
    for i in range(6):
        row=core_bytes(symbols["login_issued_resources"]+i*40,40);pointer,nbytes=struct.unpack_from("<2I",row)
        text=core_bytes(pointer,64).split(b"\0",1)[0].decode("ascii")
        body=payload.get(text)
        if body is None or len(body)!=nbytes or row[8:].hex()!=digest(body):raise ValueError("CORE内实际资源发行不同: "+text)
        compiled_resources.add(text)
    if compiled_resources!=required:raise ValueError("CORE内login资源表范围不同")
    for name in HELPERS: pins.values[ROOT/"tools"/name] = sha(ROOT/"tools"/name)
    for name in ("model.py", "driver.py"): pins.values[HERE/name] = sha(HERE/name)
    plan.update(build=str(build), build_report=str(proof_path), build_report_sha256=sha(proof_path),
        p6_reports=p6_proofs, data=str(data_path), source_data_sha256=config["data"]["sha256"],bootstrap_dependency_order=bootstrap_order,
        runtime_input_sha256=runtime, bootstrap=bootstrap, manifests=manifests,
        ordinary_libraries_generated_this_vm=[key for key,value in plan["cases"].items() if value[2]],
        standard_library_behavior_scope="REGENERATE_ONLY_EXISTING_P3_BEHAVIOR_EVIDENCE_REUSED_EXTERNALLY_NO_MATRIX_RERUN",
        old_g1=old_origin, task_layout=layout, native_seed_manifest=seed,
        payload_sha256={name:digest(body) for name,body in payload.items()},
        generated_libraries_installed=False, generated_applications_executed=False, full_M10a2=False)
    return plan, payload, pins


def strings_at(data, offset, count):
    if not 0 < count <= len(data)-offset: raise ValueError("字符串表范围错误")
    strings = data[offset:offset+count]
    if strings[-1]: raise ValueError("字符串表没有NUL结束")
    def string(index):
        if index >= count or (index and strings[index-1]): raise ValueError("字符串引用不在起点")
        end = strings.index(0, index)
        return strings[index:end].decode("utf-8")
    return string


def source_record(path, nbytes, fnv, payload, prefixes=("SYS/SRC/", "SYS/INC/")):
    guest = posixpath.normpath("/"+path.lstrip("/"))[1:]
    if not guest.startswith(prefixes) or guest not in payload: raise ValueError("来源不属于本批SRC/INC: " + path)
    body = payload[guest]; actual = 2166136261
    for value in body: actual = ((actual ^ value)*16777619) & 0xffffffff
    if len(body) != nbytes or actual != fnv: raise ValueError("源正文/FNV不同: " + path)
    return dict(path=path, guest=guest, bytes=nbytes, fnv1a32=fnv, sha256=digest(body))


def object_contract(data, payload, manifest, prefixes=("SYS/SRC/", "SYS/INC/")):
    if len(data) < 96 or data[:8] != b"SCO1MIO\0": raise ValueError("不是SCO v1")
    version, head, machine, flags, size, sec, nsec, sy, nsy, reoff, nrel, fi, nf, li, nl, st, ns = struct.unpack_from("<17I",data,8)
    if (version,head,machine,flags,size,nsec) != (1,96,3,0,len(data),4): raise ValueError("SCO头不规范")
    at = 96
    for offset,count,width in ((sec,nsec,24),(sy,nsy,32),(reoff,nrel,24),(fi,nf,16),(li,nl,16),(st,ns,1)):
        if offset != at or count > (len(data)-at)//width: raise ValueError("SCO表边界错误")
        at += count*width
    text = strings_at(data,st,ns); sources=[]; imported=[]; sections=[]; definitions=[]; lines=[]
    for i in range(4):
        kind,offset,nbytes,memory,alignment,access=struct.unpack_from("<6I",data,sec+i*24)
        if kind!=i+1 or not alignment or alignment&(alignment-1) or alignment>4096 or nbytes>memory:
            raise ValueError("SCO实际段/对齐不规范")
        if nbytes and (offset>len(data) or nbytes>len(data)-offset):raise ValueError("SCO段正文越界")
        sections.append(dict(memory=memory,alignment=alignment,access=access))
    for i in range(nf):
        name,nbytes,fnv,reserved=struct.unpack_from("<4I",data,fi+i*16)
        if reserved: raise ValueError("SCO source reserved不为零")
        sources.append(source_record(text(name),nbytes,fnv,payload,prefixes))
    text_bytes = struct.unpack_from("<I",data,sec+12)[0]
    for i in range(nl):
        section,offset,file,line=struct.unpack_from("<4I",data,li+i*16)
        if section!=1 or offset>=text_bytes or file>=nf or not 0<line<=65535: raise ValueError("SCO源行越界")
        lines.append(dict(offset=offset,file=file,line=line))
    for i in range(nsy):
        name,binding,kind,section,value,width,abi,sflags=struct.unpack_from("<8I",data,sy+i*32)
        if not section: imported.append((text(name),kind,width))
        else:
            if not 1<=section<=4 or not width or value>=sections[section-1]["memory"] or width>sections[section-1]["memory"]-value:
                raise ValueError("SCO定义不属于实际段")
            definitions.append(dict(name=text(name),section=section,value=value,size=width,kind=kind,binding=binding))
    wanted={(i["name"],1 if i["kind"]=="function" else 2,i["size"]) for i in manifest["imports"]}
    if set(imported)!=wanted or len(imported)!=len(wanted): raise ValueError("guest SCO实际imports与正式NATIVE.JSON不同")
    return dict(format="SCO1MIO", bytes=len(data),sha256=digest(data),sources=sources,line_count=nl,symbol_count=nsy,relocations=nrel,
        sections=sections,definitions=definitions,lines=lines,source_contract_exact=True)


def debug_contract(data, image, tables, payload, objects, prefixes=("SYS/SRC/", "SYS/INC/")):
    if len(data)<96 or data[:8]!=b"SCDB1MIO": raise ValueError("不是SCDB v1")
    version,head,machine,kind,nbytes,nobjects=struct.unpack_from("<6I",data,8)
    library=image[:8]==b"SCL1MIO\0"
    if (version,head,machine,kind,nbytes,nobjects)!=(1,96,3,2 if library else 1,len(image),len(objects)) or data[32:64]!=hashlib.sha256(image).digest():
        raise ValueError("SCDB未绑定实际image/地址域/对象数")
    sy,ns,fi,nf,li,nl,st,nst=struct.unpack_from("<8I",data,64)
    if (sy,fi,li,st,len(data))!=(96,96+ns*24,96+ns*24+nf*20,96+ns*24+nf*20+nl*16,96+ns*24+nf*20+nl*16+nst): raise ValueError("SCDB表边界不同")
    text=strings_at(data,st,nst); sources=[]
    expected=[(j,s) for j,obj in enumerate(objects) for s in obj["sources"]]
    if nf!=len(expected): raise ValueError("SCDB源表不是实际SCO源表")
    for i in range(nf):
        owner,name,nbytes,fnv,reserved=struct.unpack_from("<5I",data,fi+i*20)
        source=source_record(text(name),nbytes,fnv,payload,prefixes); obj,wanted=expected[i]
        if reserved or owner!=obj or source!=wanted: raise ValueError("SCDB改变对象/源身份")
        sources.append(source)
    segments=tables["segments"] if library else [dict(rva=0x400000,mem_size=tables["program_load_bytes"]+tables["program_bss_bytes"],flags=7)]
    def mapped(address,size,execute=False):
        return size>0 and any(address>=s["rva"] and address-s["rva"]<s["mem_size"] and size<=s["mem_size"]-(address-s["rva"]) and (not execute or s["flags"]&4) for s in segments)
    # 复核实际SCO段布局，不接受仅“位于映像内”的随意符号/行地址。
    align=lambda value,boundary:(value+boundary-1)&-boundary
    cursor=0 if library else 64;rvas=[[0]*4 for _ in objects]
    for group in range(4):
        alignment=max([obj["sections"][group]["alignment"] for obj in objects if obj["sections"][group]["memory"]] or [1])
        cursor=align(cursor,4096 if library else alignment);start=cursor
        for index,obj in enumerate(objects):
            section=obj["sections"][group]
            if section["memory"]:cursor=align(cursor,section["alignment"])
            rvas[index][group]=cursor;cursor+=section["memory"]
        if group==3 and not library:
            shift=tables["program_load_bytes"]-start
            for row in rvas:row[3]+=shift
        if library and cursor!=start:cursor=align(cursor,4096)
    base=0 if library else 0x400000
    expected_symbols=[(s["name"],base+rvas[j][s["section"]-1]+s["value"],s["size"],s["kind"],s["binding"],j)
                      for j,obj in enumerate(objects) for s in obj["definitions"]]
    if ns!=len(expected_symbols):raise ValueError("SCDB符号数不是实际SCO定义")
    for i in range(ns):
        name,address,size,tag,binding,owner=struct.unpack_from("<6I",data,sy+i*24)
        if (text(name),address,size,tag,binding,owner)!=expected_symbols[i] or not mapped(address,size,tag==1): raise ValueError("SCDB符号不是实际SCO定义/地址")
    expected_lines=[];file_base=0
    for j,obj in enumerate(objects):
        expected_lines.extend((base+rvas[j][0]+row["offset"],file_base+row["file"],row["line"],j) for row in obj["lines"])
        file_base+=len(obj["sources"])
    if nl!=len(expected_lines):raise ValueError("SCDB丢失真实SCO行")
    for i in range(nl):
        address,file,line,owner=struct.unpack_from("<4I",data,li+i*16)
        if file>=nf or owner>=len(objects) or expected[file][0]!=owner or not 0<line<=65535 or not mapped(address,1,True) or (address,file,line,owner)!=expected_lines[i]: raise ValueError("SCDB源行不是实际SCO行/地址")
    if nl!=sum(obj["line_count"] for obj in objects): raise ValueError("SCDB丢失真实SCO行")
    return dict(format="SCDB1MIO",image_sha256=digest(image),bytes=len(data),sha256=digest(data),symbols=ns,files=nf,lines=nl,object_count=nobjects,source_and_address_contract_exact=True)
