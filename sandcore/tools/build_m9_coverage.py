#!/usr/bin/env python3
"""将两来源的真实PASS用例逐项映射；只称已公开行为子集通过。"""
import csv
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
CORE={
    'shell-quote':['sh','printf'],'shell-function':['sh'],'shell-loop':['sh'],'shell-condition':['sh'],
    'shell-case':['sh'],'shell-parameters':['sh'],'shell-substitute':['sh'],'shell-arithmetic':['sh'],
    'shell-here':['sh'],'shell-here-quoted':['sh'],'shell-here-tabs':['sh'],'shell-short-circuit':['sh'],
    'shell-background-status':['sh'],'cat-binary':['cat'],'grep-pipeline':['grep'],'sed-substitute':['sed'],
    'awk-sum':['awk'],'awk-array':['awk'],'awk-gsub':['awk'],'awk-short-circuit':['awk'],
    'sort-unique':['sort','uniq'],'cut':['cut'],'tr':['tr'],'head-tail':['head','tail'],'rev':['rev'],
    'tac':['tac'],'paste':['paste'],'fold':['fold'],'expr':['expr'],'dc':['dc'],'seq':['seq'],
    'basename':['basename'],'dirname':['dirname'],'test':['test'],'false':['false'],'true':['true'],
    'xargs':['xargs'],'gzip-roundtrip':['gzip','gunzip'],'bzip2-roundtrip':['bzip2','bunzip2'],
    'uu-roundtrip':['uuencode','uudecode'],'readlink':['readlink'],'storage-flush':['sync','fsync'],
    'mktemp':['mktemp'],'md5-known-answer':['md5sum'],'sha1-known-answer':['sha1sum'],
    'sha256-known-answer':['sha256sum'],'sha512-known-answer':['sha512sum'],
    'cpio-roundtrip':['cpio'],'ar-roundtrip':['ar'],'unzip-deflate':['unzip'],'manual-cli-page':['man'],
    'cron-matching-shell':['crond'],'bzip2-external-fixture':['bzcat'],'lzma-external-fixture':['lzmacat'],
    'bzip2-compress-oracle':['bzip2'],'bzip2-independent-decoder':['bzip2'],
    'truncated-BZIP-preserves-target':['bunzip2'],'truncated-LZMA-preserves-target':['unlzma']}


def main():
    rows=list(csv.DictReader((ROOT/'docs/M9-CLI-MATRIX.tsv').open(encoding='utf-8'),delimiter='\t'))
    evidence={}
    batches=[('m9-final-cli-core-20261005-02',CORE),('m9-cli-behavior-20261005-03',None),('m9-security-extra-20261005-07',None)]
    for batch,mapping in batches:
        for disk in (1,2):
            path=ROOT/'build'/batch/f'disk-{disk}/verification.json';report=json.loads(path.read_text(encoding='utf-8'))
            assert report['status']!='FAIL'
            for case in report['cases']:
                assert case['status']=='PASS'
                refs=mapping.get(case['name'],[]) if mapping else [case.get('reference')]
                for ref in refs:
                    if not ref:continue
                    evidence.setdefault(ref,[]).append(dict(disk=disk,report=path.relative_to(ROOT).as_posix(),case=case['name']))
    implemented={r['reference'] for r in rows if r['source']!='-'}
    tested={r for r,cases in evidence.items() if {c['disk'] for c in cases}=={1,2}}
    missing=implemented-tested
    assert not missing,sorted(missing)
    for row in rows:
        if row['reference'] in implemented:
            row['state']='BEHAVIOR-SUBSET-PASS'
            row['acceptance']='M9-CLI-BEHAVIOR.json:'+row['reference']
    catalog=ROOT/'docs/M9-CLI-MATRIX.tsv'
    with catalog.open('w',encoding='utf-8',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=rows[0].keys(),delimiter='\t',lineterminator='\n');writer.writeheader();writer.writerows(rows)
    result=dict(status='DECLARED_LOCAL_CLI_SUBSETS_PASS',user_approved_denominator='M9-local',
                reference_total=len(rows),local_total=sum(r['applicability']=='M9-local' for r in rows),
                tested_local_commands=len(implemented),local_reference_command_ratio=100*len(implemented)/171,
                original_reference_command_ratio=100*len(implemented)/len(rows),
                not_full_BusyBox_options_compatibility=True,aliases_share_behavior=['hd/hexdump'],
                missing_local=[r['reference'] for r in rows if r['applicability']=='M9-local' and r['source']=='-'],
                evidence={r:evidence[r] for r in sorted(implemented)},catalog_sha256=hashlib.sha256(catalog.read_bytes()).hexdigest())
    (ROOT/'docs/M9-CLI-BEHAVIOR.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f'{len(implemented)}/171 local reference commands have real two-source behavior evidence; all {len(rows)} rows kept')


if __name__=='__main__':main()
