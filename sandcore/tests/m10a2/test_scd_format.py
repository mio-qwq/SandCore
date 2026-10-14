#!/usr/bin/env python3
"""mio：一轮动态SCX真实REL/原生后端/格式及旧字节的定向宿主检查。"""

from __future__ import annotations

import copy
import ctypes
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/m10a2-dynamic-host"
sys.path.insert(0, str(ROOT / "tools"))
import mkdscx
import mkscl
import scd_format as scd
import scl_format as scl

BASELINE = "d43bdd10ee859a72e120eed6eebec6a4f95d0cef"
ADAPTER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
typedef unsigned char u8;
typedef unsigned int u32;
static const char *host_root,*host_cwd;
static int host_fail_map,host_resolve_calls,cli_argc,build_view_cancel;
static char **cli_argv;
static char host_gui[1024];
static int length(const char *p){return (int)strlen(p);}
static int equal(const char *a,const char *b){return !strcmp(a,b);}
static void copy(char *d,const char *s,int n){if(n)snprintf(d,(size_t)n,"%s",s);}
static void append(char *d,const char *s,int n){int used=length(d);if(used<n)copy(d+used,s,n-used);}
static void decimal(char *d,int n){sprintf(d,"%d",n);}
static void sc_exit(int n){exit(n);}
static void sc_yield(void){}
static void sc_puts(const char *p){fputs(p,stderr);}
static void *sc_alloc(u32 n){return malloc(n);}
static void sc_free(void *p){free(p);}
static const char *host_path(const char *p){
    static char path[1024];while(*p=='/')p++;
    int at=snprintf(path,sizeof(path),"%s/",host_root);
    while(*p && at<(int)sizeof(path)-1){int c=(u8)*p++;path[at++]=(char)(c>='a' && c<='z'?c-32:c);}
    path[at]=0;return path;
}
static int sc_stat(const char *p,u32 *info){struct stat s;if(stat(host_path(p),&s))return -1;
    info[0]=S_ISDIR(s.st_mode)?2:1;info[1]=(u32)s.st_size;return 0;}
static int sc_read_at(const char *p,void *out,int n,int offset){FILE *f=fopen(host_path(p),"rb");if(!f)return -1;
    fseek(f,offset,SEEK_SET);int got=(int)fread(out,1,(size_t)n,f);fclose(f);return got;}
static int sc_read(const char *p,void *out,int n){return sc_read_at(p,out,n,0);}
static int sc_write(const char *p,const void *data,int n){
    if(host_fail_map && length(p)>=4 && equal(p+length(p)-4,".map"))return -1;
    FILE *f=fopen(host_path(p),"wb");if(!f)return -1;int wrote=(int)fwrite(data,1,(size_t)n,f);fclose(f);return wrote;}
static int build_view_poll(const char *p,int a,int b,int c){(void)p;(void)a;(void)b;(void)c;return 0;}
static void build_view_result(const char *p,int failed){(void)failed;fputs(p,stderr);}
static void build_view_begin(const char *title,const char *source,const char *output){
    (void)title;fprintf(stderr,"ACTUAL_MAIN_SOURCE=%s OUTPUT=%s\n",source,output);}
static void build_view_wait(void){}
static int cli_parse(void){return 0;}
static int cli_text(int fd,const char *p){fputs(p,fd==2?stderr:stdout);return length(p);}
static int sc_arguments2(char *p,int n){if(length(host_gui)>=n)return -1;copy(p,host_gui,n);return length(p);}
static void sc_args(char *p,int n){(void)sc_arguments2(p,n);}
static char *token(char **p){while(**p==' ')(*p)++;char *begin=*p;while(**p && **p!=' ')(*p)++;
    if(**p)*(*p)++=0;return begin;}
/* ROOTLESS是旧ABI的真实返回语义；FS访问不会再次按cwd解释名字。 */
static int sc_resolve(const char *input,char *out,int capacity){
    char joined[1024],resolved[64];int used=0,offset=0;host_resolve_calls++;
    if(*input=='/')snprintf(joined,sizeof(joined),"%s",input);
    else snprintf(joined,sizeof(joined),"%s/%s",host_cwd,input);
    while(joined[offset]){
        while(joined[offset]=='/')offset++;if(!joined[offset])break;
        int start=offset;while(joined[offset] && joined[offset]!='/'){
            if((u8)joined[offset]<33 || joined[offset]=='\\' || joined[offset]==':')return -1;offset++;}
        int n=offset-start;if(n==1 && joined[start]=='.')continue;
        if(n==2 && joined[start]=='.' && joined[start+1]=='.'){
            if(!used)return -1;while(used && resolved[used-1]!='/')used--;if(used)used--;continue;}
        if(used+n+(used!=0)>63)return -1;if(used)resolved[used++]='/';
        memcpy(resolved+used,joined+start,(size_t)n);used+=n;
    }
    if(used>=capacity)return -1;resolved[used]=0;memcpy(out,resolved,(size_t)used+1);return used;
}
'''
WRAPPER = r'''
int main(int argc,char **argv){
    if(argc!=8)return 2;host_cwd=argv[1];host_root=argv[2];host_fail_map=atoi(argv[7]);
    char *args[4]={argv[3],argv[4],argv[5],argv[6]};cli_argv=args;
    cli_argc=equal(argv[5],"--legacy")?2:equal(argv[5],"--skm")?3:4;
    snprintf(host_gui,sizeof(host_gui),"%s %s",argv[3],argv[4]);
    if(cli_argc>=3){append(host_gui," ",sizeof(host_gui));append(host_gui,argv[5],sizeof(host_gui));}
    if(cli_argc==4){append(host_gui," ",sizeof(host_gui));append(host_gui,argv[6],sizeof(host_gui));}
    int result=sccc_actual_main();
    fprintf(stderr,"\nROOTLESS_RESOLVE_CALLS=%d RESULT=%d tokens=%d source=%d code=%d data=%d bss=%d fixes=%d macros=%d\n",
        host_resolve_calls,result,token_count,source_used,code_used,data_used,bss_used,fix_count,macro_count);return result;
}
'''


def run(args, success=True):
    result = subprocess.run(args, cwd=ROOT, capture_output=True, text=True)
    if success and result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result


def build_sccc(label: str, cli: bool = True, baseline: bool = False) -> Path:
    def read(name):
        if baseline:
            return run(["git", "show", f"{BASELINE}:sandcore/user/{name}"]).stdout
        return (ROOT / "user" / name).read_text(encoding="utf-8")

    source = read("s3c.c").replace('#include "SCAPI.H"', ADAPTER)
    for name in ("COMPILECLI.inc", "COMPILEUI.inc"):
        source = source.replace(f'#include "{name}"', "")
    for name in ("s3c_lex.inc", "s3c_pp.inc", "s3c_parse.inc", "s3c_std_parse.inc",
                 "s3c_emit.inc", "s3c_std_emit.inc", "s3c_scl.inc"):
        if f'#include "{name}"' in source:source = source.replace(f'#include "{name}"', read(name))
    source = source.replace("int main(void)", "static int sccc_actual_main(void)")
    path = OUT / (label + ".c")
    path.write_text(("#define SCCC_CLI 1\n" if cli else "") + source + WRAPPER, encoding="utf-8")
    target = OUT / label
    result = run(["gcc", "-O2", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                  "-Wno-unused-variable", "-Wno-misleading-indentation", "-Iuser", str(path), "-o", str(target)])
    (OUT / (label + "-build.log")).write_text(result.stdout + result.stderr)
    return target


class ScdFormatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if shutil.which("gcc") is None:
            raise unittest.SkipTest("run this suite using WSL GCC")
        OUT.mkdir(parents=True, exist_ok=True)
        cls.vfs = OUT / "VFS"
        for path in ("HOME", "TMP/DYNAMIC", "SYS/SRC/RUNTIME", "SYS/INC/RUNTIME"):
            (cls.vfs / path).mkdir(parents=True, exist_ok=True)
        for name in ("DYNAPP.C", "DYNAPP.json", "NATIVEBASE.C", "NATIVEBASE.json", "NATIVEUPPER.C", "NATIVEUPPER.json"):
            (cls.vfs / "TMP/DYNAMIC" / name.upper()).write_bytes((ROOT / "tests/m10a2" / name).read_bytes())
        cls.manifest = mkdscx.load_manifest(ROOT / "tests/m10a2/DYNAPP.json")
        cls.object = OUT / "DYNAPP.o"
        flags = ["-x", "c", "-m32", "-std=c11", "-O0", "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-builtin",
                 "-fno-pie", "-fno-pic", "-fno-stack-protector", "-fno-common", "-fno-asynchronous-unwind-tables",
                 "-fno-unwind-tables", "-msoft-float", "-mno-sse", "-mno-mmx"]
        run(["gcc", *flags, "-c", "tests/m10a2/DYNAPP.C", "-o", str(cls.object)])
        cls.host_blob = mkdscx.pack_elf(cls.object.read_bytes(), cls.manifest)
        (OUT / "DYNAPP.SCX").write_bytes(cls.host_blob)
        shared = OUT / "scd-validator.so"
        run(["gcc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-builtin",
             "-fno-stack-protector", "-fno-tree-loop-distribute-patterns", "user/runtime/scd_format.c", "-o", str(shared)])
        cls.validator = ctypes.CDLL(str(shared)).scd_validate
        cls.validator.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint]
        cls.validator.restype = ctypes.c_int
        cls.cli = build_sccc("sccc-cli")
        cls.gui = build_sccc("sccc-gui", False)
        cls.old = build_sccc("sccc-baseline", baseline=True)
        cls.records = []

    def compile(self, source, output, mode="--dynamic", manifest="/TMP/DYNAMIC/DYNAPP.json", fail_map=False, binary=None, success=True):
        return run([str(binary or self.cli), "SYS", str(self.vfs), source, output, mode, manifest,
                    "1" if fail_map else "0"], success)

    def verdicts(self, metadata, rva):
        try:
            scd.validate(metadata, rva)
            python = 0
        except scd.ScdError as exc:
            python = exc.code
        buffer = ctypes.create_string_buffer(metadata)
        return python, self.validator(buffer, len(metadata), rva)

    def test_01_real_host_and_native_dynamic(self):
        parsed = scd.validate_scx(self.host_blob)
        bridge = scd.crt(scd.PROGRAM_BASE + parsed["metadata_rva"])
        self.assertEqual(len(bridge), 64)
        self.assertEqual(bridge[27:32], bytes.fromhex("83 ec 08 52 68"))
        self.assertEqual(bridge[36:41], bytes.fromhex("ff d0 83 c4 30"))
        self.assertEqual(bridge[19:21], bytes.fromhex("78 1c"))
        self.assertEqual(bridge[49:52], bytes.fromhex("83 c4 20"))
        self.assertEqual(struct.unpack_from("<I", bridge, 6)[0], struct.unpack_from("<I", bridge, 32)[0])
        initial_esp = 0x800000 - 16
        before_call = initial_esp - 32 - 8 - 4 - 4
        self.assertEqual(before_call % 16, 0)
        self.assertEqual(before_call + 48, initial_esp)
        self.assertEqual({r["type"] for r in parsed["relocations"]}, {scd.ABS32, scd.PC32})
        for binary, label in ((self.cli, "CLI"), (self.gui, "GUI")):
            result = self.compile("/TMP/DYNAMIC/DYNAPP.C", "/TMP/DYNAMIC/DYNAPP.SCX", binary=binary)
            blob = (self.vfs / "TMP/DYNAMIC/DYNAPP.SCX").read_bytes()
            native = scd.validate_scx(blob)
            self.assertEqual(self.verdicts(blob[36 + native["metadata_rva"]:], native["metadata_rva"]), (0, 0))
            self.assertIn("ROOTLESS_RESOLVE_CALLS=3 RESULT=0", result.stderr)
            self.assertEqual([i["name"] for i in native["imports"]], [i["name"] for i in parsed["imports"]])
            self.records.append(dict(case="native-actual-main-" + label, status="PASS", stderr=result.stderr,
                                     bytes=len(blob), sha256=hashlib.sha256(blob).hexdigest()))
        self.native_blob = blob
        self.__class__.native_blob = blob
        self.records.append(dict(case="host-REL-and-C-validator", status="PASS", bytes=len(self.host_blob),
                                 sha256=hashlib.sha256(self.host_blob).hexdigest()))

    def test_02_bss_and_import_pointer_addresses(self):
        for label, blob in (("HOST", self.host_blob), ("NATIVE", self.native_blob)):
            parsed = scd.validate_scx(blob)
            imports = [0x50000000 + i * 256 for i in range(len(parsed["imports"]))]
            image = scd.relocated_image(blob, imports)
            if label == "HOST":
                elf = mkscl.ElfObject(self.object.read_bytes())
                _, _, alignment, bss_sections = mkdscx.application_layout(elf)
                for s in bss_sections:
                    s.rva += parsed["program_load_bytes"]
                addresses = {name: scd.PROGRAM_BASE + elf.symbol_rva(s) for name, s in elf.globals.items()
                             if s.section != mkscl.SHN_UNDEF}
                self.assertEqual(parsed["program_load_bytes"] % alignment, 0)
            else:
                addresses = {p[2]: int(p[1], 16) for line in (self.vfs / "TMP/DYNAMIC/DYNAPP.SCX.MAP").read_text().splitlines()
                             if (p := line.split()) and p[0] == "OBJECT"}
            for pointer, object_name in (("application_bss_pointer", "application_bss"),
                                         ("application_value_pointer", "application_value"),
                                         ("application_label_pointer", "application_label")):
                offset = addresses[pointer] - scd.PROGRAM_BASE
                self.assertEqual(struct.unpack_from("<I", image, offset)[0], addresses[object_name])
            self.assertEqual(image[parsed["program_load_bytes"]:], bytes(parsed["program_bss_bytes"]))
            for r in parsed["relocations"]:
                value = imports[r["import_index"]] + r["addend"]
                if r["type"] == scd.PC32:
                    value -= scd.PROGRAM_BASE + r["patch_rva"]
                self.assertEqual(struct.unpack_from("<I", image, r["patch_rva"])[0], value & 0xFFFFFFFF)
        self.records.append(dict(case="internal-BSS-RO-and-external-ABS-PC-addresses", status="PASS"))

    def test_03_c_python_invalid_metadata_agrees(self):
        parsed = scd.validate_scx(self.host_blob)
        rva = parsed["metadata_rva"]
        metadata = self.host_blob[36 + rva:]
        words = lambda off: struct.unpack_from("<I", metadata, off)[0]
        dep, imp, rel = (words(i) for i in (32, 40, 48))
        for label, offset, value, expected in (
                ("version", 8, 2, scd.E_VERSION), ("header", 12, 65, scd.E_FORMAT),
                ("exact metadata", 16, len(metadata) - 1, scd.E_RANGE),
                ("load overflow", 20, 0xFFFFFFFF, scd.E_RANGE),
                ("main CRT", 28, 0, scd.E_RANGE), ("main metadata", 28, rva, scd.E_RANGE),
                ("table multiplication", 36, 0xFFFFFFFF, scd.E_RANGE),
                ("string suffix", dep, words(dep) + 1, scd.E_SYMBOL),
                ("duplicate dependencies", dep + 20, words(dep), scd.E_SYMBOL),
                ("dependency flags", dep + 16, 1, scd.E_FORMAT),
                ("import dependency", imp + 4, 0xFFFFFFFF, scd.E_RANGE),
                ("patch CRT", rel, 0, scd.E_RANGE), ("patch metadata", rel, rva - 3, scd.E_RANGE),
                ("relocation import", rel + 8, 0xFFFFFFFF, scd.E_RANGE),
                ("relocation kind", rel + 4, 3, scd.E_FORMAT)):
            with self.subTest(label=label):
                candidate = bytearray(metadata)
                struct.pack_into("<I", candidate, offset, value)
                self.assertEqual(self.verdicts(bytes(candidate), rva), (expected, expected))
        self.records.append(dict(case="representative-invalid-SCD-C-Python-agreement", status="PASS"))

    def test_04_strict_manifest_and_failure_preserve(self):
        output = self.vfs / "TMP/DYNAMIC/DYNAPP.SCX"
        original = output.read_bytes()
        result = self.compile("/TMP/DYNAMIC/DYNAPP.C", "/TMP/DYNAMIC/DYNAPP.SCX", fail_map=True, success=False)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(output.read_bytes(), original)
        for label, mutate in (("library-field", lambda m: m.update(exports=[])),
                              ("wrong-kind", lambda m: m.update(kind="library")),
                              ("small-data-import", lambda m: m["imports"][2].update(size=1)),
                              ("duplicate-import", lambda m: m["imports"].append(m["imports"][0]))):
            manifest = copy.deepcopy(self.manifest)
            mutate(manifest)
            path = self.vfs / "TMP/DYNAMIC/BAD.JSON"
            path.write_text(json.dumps(manifest))
            result = self.compile("/TMP/DYNAMIC/DYNAPP.C", "/TMP/DYNAMIC/DYNAPP.SCX", manifest="/TMP/DYNAMIC/BAD.json", success=False)
            self.assertEqual(result.returncode, 1, label)
            self.assertEqual(output.read_bytes(), original, label)
            if label != "small-data-import":
                with self.assertRaises(mkdscx.PackError):
                    mkdscx.normalize_manifest(manifest)
        alias = self.vfs / "HOME/S3C.LOG"
        source = (ROOT / "tests/m10a2/DYNAPP.C").read_bytes()
        alias.write_bytes(source)
        long_path = "/TMP/" + "D" * 50 + "/DYN.SCX"
        self.assertEqual(len(long_path) - 1, 62)
        result = self.compile("/HOME/S3C.log", long_path, success=False)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(alias.read_bytes(), source)
        self.assertIn("ROOTLESS_RESOLVE_CALLS=3 RESULT=2", result.stderr)
        bad = self.vfs / "TMP/DYNAMIC/BAD.JSON"
        bad.write_text('{"kind":"application","kind":"application","name":"DYNAPP.SCX","dependencies":[],"imports":[]}')
        with self.assertRaises(mkdscx.PackError):
            mkdscx.load_manifest(bad)
        self.records.append(dict(case="map-manifest-and-rootless-sidecar-failures-preserve-body-and-input", status="PASS"))

    def test_05_legacy_bytes_and_scl_still_native(self):
        for name, body, mode in (("OLD.C", "int value=7;int state;int add(int x){return x+value;}int main(void){state=add(5);return state-12;}", "--legacy"),
                                 ("MODULE.C", "int value=7;int state;int skm_main(void *api){state=value;return api!=0;}", "--skm")):
            (self.vfs / "TMP/DYNAMIC" / name).write_text(body)
            outputs = []
            for binary, tag in ((self.old, "OLD"), (self.cli, "CURRENT")):
                output = f"/TMP/DYNAMIC/{tag}.BIN"
                self.compile("/TMP/DYNAMIC/" + name, output, mode=mode, binary=binary)
                outputs.append((self.vfs / output[1:]).read_bytes())
            self.assertEqual(outputs[0], outputs[1])
        for part in ("BASE", "UPPER"):
            self.compile(f"/TMP/DYNAMIC/NATIVE{part}.C", f"/TMP/DYNAMIC/{part}.SCL", mode="--scl",
                         manifest=f"/TMP/DYNAMIC/NATIVE{part}.json")
            scl.validate((self.vfs / f"TMP/DYNAMIC/{part}.SCL").read_bytes())
        self.records.append(dict(case="legacy-SCX-SKM-representative-byte-identity-and-nine-field-SCL", status="PASS", baseline=BASELINE))

    def test_06_current_self_source_capacity(self):
        names = ["sccc.c", "s3c.c", "s3c_lex.inc", "s3c_pp.inc", "s3c_parse.inc", "s3c_std_parse.inc",
                 "s3c_emit.inc", "s3c_std_emit.inc", "s3c_scl.inc",
                 "SCAPI.H", "SCIO.H", "COMPILECLI.inc", "runtime/SCLFMT.H", "runtime/scl_format.c",
                 "runtime/SCDYN.H", "runtime/scd_format.c"]
        for name in names:
            (self.vfs / "SYS/SRC" / name.upper()).write_bytes((ROOT / "user" / name).read_bytes())
            (self.vfs / "SYS/INC" / name.upper()).write_bytes((ROOT / "user" / name).read_bytes())
        result = self.compile("/SYS/SRC/SCCC.C", "/HOME/SCCC-GENERATED.SCX", mode="--legacy")
        self.records.append(dict(case="real-native-backend-current-self-source-capacity", status="PASS", stderr=result.stderr,
                                 executes_generated_compiler=False))

    @classmethod
    def tearDownClass(cls):
        paths = ("user/s3c.c", "user/s3c_parse.inc", "user/s3c_std_parse.inc", "user/s3c_emit.inc",
                 "user/s3c_std_emit.inc", "user/s3c_pp.inc", "user/s3c_lex.inc", "user/s3c_scl.inc",
                 "user/runtime/SCDYN.H", "user/runtime/scd_format.c", "tools/scd_format.py", "tools/mkdscx.py",
                 "tests/m10a2/DYNAPP.C", "tests/m10a2/DYNAPP.json", "tests/m10a2/test_scd_format.py")
        report = dict(scope="DYNAMIC_SCX_HOST_REAL_LINK_AND_NATIVE_EMITTER", executes_i386=False, guest_sccc=False,
                      cases=cls.records, input_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in paths})
        (OUT / "report.json").write_text(json.dumps(report, indent=2))


if __name__ == "__main__":
    unittest.main(verbosity=2)
