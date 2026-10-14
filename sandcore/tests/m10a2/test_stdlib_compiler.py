#!/usr/bin/env python3
"""新SDK实际前端/生成容量与诊断事务；生成i386执行由guest单列。"""
import hashlib
import json
from pathlib import Path
import sys
import unittest

import test_scd_format as backend

ROOT=backend.ROOT
OUT=ROOT/'build/m10a2-stdlib-host'

class StdlibCompilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        OUT.mkdir(parents=True,exist_ok=True);backend.OUT=OUT
        cls.compiler=backend.build_sccc('SCCC-current-stdlib')
        cls.vfs=OUT/'VFS'
        for name in ('TMP','HOME','SYS/INC/RUNTIME','SYS/SRC/RUNTIME'):
            (cls.vfs/name).mkdir(parents=True,exist_ok=True)
        cls.records=[]
    def compile(self,label,source,expected_error=None):
        name=label.upper();(self.vfs/'TMP'/ (name+'.C')).write_text(source,encoding='utf-8')
        output=self.vfs/'HOME'/(name+'.SCX');sentinel=b'PREVIOUS_GOOD_OUTPUT';output.write_bytes(sentinel)
        command=[str(self.compiler),'/',str(self.vfs),'/TMP/'+name+'.C','/HOME/'+name+'.SCX','--legacy','unused','0']
        result=backend.run(command,success=False)
        if expected_error:
            self.assertNotEqual(result.returncode,0,result.stderr)
            self.assertIn(expected_error,result.stderr)
            self.assertEqual(output.read_bytes(),sentinel)
        else:
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertNotEqual(output.read_bytes(),sentinel)
        self.records.append(dict(case=label,status='PASS',diagnostic=result.stderr,
            rejected=bool(expected_error),output_preserved=bool(expected_error),executes_generated_program=False))
    def test_01_real_scalar_varargs_offset_and_unsigned_frontend(self):
        self.compile('STD-SCALARS',r'''
#if 0xffffffffu <= 0x7fffffff
#error unsigned comparison lost its type
#endif
#if (0xffffffffu >> 31u) != 1u || ((1 ? -1 : 0u) < 0)
#error unsigned shift or conditional lost its type
#endif
#if 0 && (1 / 0)
#error short circuit evaluated the unused expression
#endif
typedef unsigned char *va_list;
struct Item {char prefix;_Bool flag;int value;};
enum {OFF=__builtin_offsetof(struct Item,value)};
_Bool truth=256;
static int visit(void *p){return p!=0;}
static int sum(int count,...){va_list ap,copy;__builtin_va_start(ap,count);__builtin_va_copy(copy,ap);
    int x=__builtin_va_arg(ap,int);int y=__builtin_va_arg(copy,int);
    __builtin_va_end(ap);__builtin_va_end(copy);return x+y;}
int main(void){int c=0;void *p=&c;_Bool b=(void*)256;const char *name=__func__;
    return visit(c?0:p)+visit(c?p:0)+sum(1,7)+b+truth+OFF+(name[0]=='m');}
''')
    def test_02_specific_invalid_interfaces_keep_old_output(self):
        cases=(('BAD-CV','static void use(int *p){} int main(void){int c=1,x;int *rw=&x;const int *ro=&x;use(c?rw:ro);return 0;}','discards pointed-to qualifiers'),
            ('BAD-VA-START','typedef unsigned char *va_list;int main(void){va_list ap;__builtin_va_start(ap,ap);return 0;}','variadic function'),
            ('BAD-VA-NARROW','typedef unsigned char *va_list;int main(void){va_list ap;return __builtin_va_arg(ap,char);}','promoted 32-bit'),
            ('BAD-VA-VOID','typedef unsigned char *va_list;int main(void){va_list ap;if(__builtin_va_end(ap))return 1;return 0;}','integer or pointer scalar'),
            ('BAD-OVERFLOW','unsigned int x=0x100000000;int main(void){return 0;}','exceeds the 32-bit profile'),
            ('BAD-LL','int x=1LL;int main(void){return 0;}','64-bit integer suffixes'),
            ('BAD-UU','int x=1UU;int main(void){return 0;}','repeats its unsigned suffix'),
            ('BAD-MALLOC-HEADER','int main(void){return malloc(16)!=0;}','declared SDK header'),
            ('BAD-OFFSET','struct A {int x;};int main(void){return __builtin_offsetof(struct A,unknown);}','member does not exist'))
        for label,source,error in cases:
            with self.subTest(label=label):self.compile(label,source,error)
    def test_03_current_self_source_capacity(self):
        names=('sccc.c','s3c.c','s3c_lex.inc','s3c_pp.inc','s3c_parse.inc','s3c_std_parse.inc',
            's3c_emit.inc','s3c_std_emit.inc','s3c_scl.inc','SCAPI.H','SCIO.H','COMPILECLI.inc',
            'runtime/SCLFMT.H','runtime/scl_format.c','runtime/SCDYN.H','runtime/scd_format.c')
        for name in names:
            blob=(ROOT/'user'/name).read_bytes()
            for base in ('SYS/INC','SYS/SRC'):(self.vfs/base/name.upper()).write_bytes(blob)
        result=backend.run([str(self.compiler),'/',str(self.vfs),'/SYS/SRC/SCCC.C','/HOME/SCCC-G1.SCX','--legacy','unused','0'],success=False)
        self.assertEqual(result.returncode,0,result.stderr)
        self.records.append(dict(case='real-emitter-self-capacity',status='PASS',diagnostic=result.stderr,
            executes_generated_compiler=False))
    @classmethod
    def tearDownClass(cls):
        names=('s3c.c','s3c_lex.inc','s3c_pp.inc','s3c_parse.inc','s3c_std_parse.inc','s3c_emit.inc','s3c_std_emit.inc','s3c_scl.inc')
        report=dict(scope='STDLIB_COMPILER_REAL_FRONTEND_AND_CAPACITY',executes_i386=False,
            cases=cls.records,input_sha256={n:hashlib.sha256((ROOT/'user'/n).read_bytes()).hexdigest() for n in names})
        (OUT/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':unittest.main(verbosity=2)
