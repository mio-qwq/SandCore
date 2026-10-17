"""归档真实调查及失败边界；不修改07原报告，不复制客体磁盘。"""
from pathlib import Path
import gzip,hashlib,json
ROOT=Path(__file__).resolve().parents[2]
out=ROOT/'evidence/m10a2-parent-heap-02';out.mkdir(exist_ok=False)
sha=lambda b:hashlib.sha256(b).hexdigest()
files={}
for relative in ('build/m10a2-parent-heap-observe-01/heap-review.json',
                 'build/m10a2-parent-heap-observe-02/heap-review.json',
                 'build/m10a2-main-studio-01/p6-unified-host-14.json',
                 'build/m10a2-p6-sco-16/report.json'):
 p=ROOT/relative;body=p.read_bytes();name=p.parent.name+'-'+p.name+'.gz'
 (out/name).write_bytes(gzip.compress(body,mtime=0))
 files[relative]=dict(archive=name,bytes=len(body),sha256=sha(body))
r=json.loads((ROOT/'build/m10a2-parent-heap-observe-02/heap-review.json').read_bytes())
assert r['status']=='DIAGNOSTIC_CLOSED_NOT_ORIGINAL_FAILURE_RESOLUTION' and r['source_disks_unchanged']
first,last=r['surveys'][0],r['surveys'][-1];old={a['address'] for a in first['arenas']}
owners={g[k]:dict(kind=k,scalar=g['scalar'],face=g['face'],pixels=g['pixels'])
 for g in last['glyph_cache']['glyphs'] for k in ('key','entry','resource')}
arenas=[]
for a in last['arenas']:
 if a['address'] in old:continue
 row={k:v for k,v in a.items() if k!='blocks'}
 row['allocations']=[dict(b,owner=owners.get(b['payload'])) for b in a['blocks'] if b['allocated']]
 assert a['bytes']==65536 and not a['dedicated'] and len(row['allocations'])==2
 assert all(b['requested']==1600 and b['owner']['kind']=='resource' and b['owner']['scalar'] in (51,57) for b in row['allocations'])
 arenas.append(row)
assert len(arenas)==1 and last['arena_pages']-first['arena_pages']==16
review=dict(status='ACTUAL_CLOCK_GLYPH_ARENA_OBSERVED_NOT_RETROACTIVE_NATIVE07_PASS',
 guest_memory_writes=False,source_disks_unchanged=True,files=files,
 original_native07_status='FAIL_OR_INTERRUPTED',original_native07_reclassified=False,
 first_glyphs=first['glyph_cache']['count'],last_glyphs=last['glyph_cache']['count'],new_arenas=arenas,
 first_diagnostic_failure='helper cache charge omitted entry and key overhead; original failure retained',
 timer_fix_host_only='SCO16 and HOST14 actual success; original Files313 cause not yet proven')
(out/'review.json').write_bytes((json.dumps(review,ensure_ascii=False,indent=2)+'\n').encode())
print(review['status'])
