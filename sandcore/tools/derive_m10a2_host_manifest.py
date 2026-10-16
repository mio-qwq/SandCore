#!/usr/bin/env python3
"""为HOST引导对象列出精确导入；不放宽正式打包器或改原生清单。"""
import argparse,json
from pathlib import Path
from mkscl import ElfObject,SHN_UNDEF
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--object',type=Path,required=True);p.add_argument('--template',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);args=p.parse_args()
    manifest=json.loads(args.template.read_text(encoding='utf-8'))
    actual={s.name for s in ElfObject(args.object.read_bytes()).symbols[1:] if s.section==SHN_UNDEF}
    declared={i['name']:i for i in manifest['imports']}
    missing=actual-set(declared)
    if missing:raise ValueError('HOST对象引用未公开的符号：'+str(sorted(missing)))
    manifest['imports']=[declared[name] for name in sorted(actual)]
    libraries={i['dependency'] for i in manifest['imports']}
    manifest['dependencies']=[d for d in manifest['dependencies'] if d['name'] in libraries]
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print('HOST_EXACT_IMPORTS',len(actual))
if __name__=='__main__':main()
