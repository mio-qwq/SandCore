"""生成隔离的、精确钉住输入摘要的SCCC源码视图；不会编辑正式user树。"""
from pathlib import Path
import argparse
import difflib
import hashlib
import json

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PIN = {
    "s3c.c": "a4470d205fe5e48181ef6e96c831858a545b0c1eb1565ee51d9c67c2e69a5b92",
    "s3c_parse.inc": "1ca3c6ca14879a6d841ae1cffb5ab6417643ce1d8b1dc1fcddbaade931cb8d09",
    "s3c_emit.inc": "74a7c0d432d38a44f408e7ac0c27d90a57b5f869296096913f634476c54965b8",
    "s3c_scl.inc": "14b6066e184fc3c845f9402181cb1df309b62ffde091f98af298ac5145408a27",
}


def replacement(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"patch anchor occurs {text.count(old)} times: {old[:100]!r}")
    return text.replace(old, new)


def patched(name, text):
    if name == "s3c.c":
        for old, new in (
            ("static int dynamic_output;", "static int dynamic_output;\nstatic int object_output;\nstatic void sco_line(int where);\nstatic void finish_sco(const char *output);"),
            ('#include "s3c_scl.inc"', '#include "s3c_scl.inc"\n#include "s3c_sco.inc"'),
            ('if(equal(icon,"--skm")){module_output=1;icon="";}', 'if(equal(icon,"--object")){object_output=1;shared_output=1;icon="";}\n    if(equal(icon,"--skm")){module_output=1;icon="";}'),
            ('if(((shared_output || dynamic_output) && cli_argc!=4) || (!(shared_output || dynamic_output) && cli_argc>3))', 'if(((shared_output || dynamic_output) && !object_output && cli_argc!=4) || (object_output && cli_argc!=3) || (!(shared_output || dynamic_output) && cli_argc>3))'),
            ('|| sc_resolve(manifest,shared_manifest_path,64)<0)return 2;', '|| (!object_output && sc_resolve(manifest,shared_manifest_path,64)<0))return 2;'),
            ('input=shared_input_path;output=shared_output_path;manifest=shared_manifest_path;', 'input=shared_input_path;output=shared_output_path;if(!object_output)manifest=shared_manifest_path;'),
            ('if(shared_output){\n        if(!*manifest)', 'if(shared_output && !object_output){\n        if(!*manifest)'),
        ):
            text = replacement(text, old, new)
        text = replacement(text, 's3c source.c output.SCX --dynamic app.json\\n', 's3c source.c output.SCX --dynamic app.json\\ns3c source.c output.SCO --object\\n')
    elif name == "s3c_parse.inc":
        text = replacement(text, 'copy(map_text,shared_output?"SCLMAP1MIO\\n":', 'copy(map_text,object_output?"SCOMAP1MIO\\n":shared_output?"SCLMAP1MIO\\n":')
    elif name == "s3c_emit.inc":
        text = replacement(text, 'static void finish_image(const char *output)\n{', 'static void finish_image(const char *output)\n{\n    if(object_output){finish_sco(output);return;}')
        text = replacement(text, 'static void emit_statement(int node)\n{\n    if(!node) return;\n    Node *n=nodes+node;\n    error_position=n->where;', 'static void emit_statement(int node)\n{\n    if(!node) return;\n    Node *n=nodes+node;\n    error_position=n->where;\n    if(object_output && n->kind!=N_BLOCK)sco_line(n->where);')
        text = replacement(text, '    hex_text(hex,mapped);', '    if(object_output)sco_line(where);\n    hex_text(hex,mapped);')
    elif name == "s3c_scl.inc":
        text = replacement(text,
            '    if(string_off>(u32)(CODE_MAX-payload_size)',
            '    /* BSS的C对齐取决于整个SCD尾部，字符串补零受旧reader支持。 */\n    string_size+=(0u-((u32)payload_size+string_off+string_size))&3u;\n    if(string_off>(u32)(CODE_MAX-payload_size)')
    return text


def prepare(out):
    out.mkdir(parents=True, exist_ok=True)
    user = ROOT / "user"
    patches = []
    # 只复制编译器输入；无镜像、旧产物或测试盘重复副本。
    names = [p.name for p in user.glob("s3c*") if p.is_file()]
    names += ["sccc.c", "SCAPI.H", "SCIO.H", "COMPILECLI.inc", "COMPILEUI.inc"]
    for name in names:
        raw = (user / name).read_bytes()
        if name in PIN:
            digest = hashlib.sha256(raw).hexdigest()
            if digest != PIN[name]:
                raise ValueError(f"pinned compiler changed: {name}: {digest}")
        old = raw.decode("utf-8-sig").replace("\r\n", "\n")
        new = patched(name, old)
        (out / name).write_text(new, encoding="utf-8", newline="\n")
        if old != new:
            patches.extend(difflib.unified_diff(old.splitlines(True), new.splitlines(True),
                fromfile=f"a/sandcore/user/{name}", tofile=f"b/sandcore/user/{name}"))
    runtime = out / "runtime"
    runtime.mkdir(exist_ok=True)
    for name in ("SCLFMT.H", "SCDYN.H", "scl_format.c", "scd_format.c"):
        (runtime / name).write_bytes((user / "runtime" / name).read_bytes())
    for name in ("SCOFMT.H", "s3c_sco.inc"):
        (out / name).write_bytes((HERE / name).read_bytes())
    (HERE / "compiler.patch").write_text("".join(patches), encoding="utf-8", newline="\n")
    (HERE / "compiler-pins.json").write_text(json.dumps(PIN, indent=2) + "\n", encoding="ascii")
    return out


if __name__ == "__main__":
    args = argparse.ArgumentParser()
    args.add_argument("--out", type=Path, default=ROOT / "build/m10a2-object-agent-source")
    print(prepare(args.parse_args().out))
