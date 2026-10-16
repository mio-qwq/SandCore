"""核Studio实际取回的两套产物与REC，不发送客体输入。"""
from pathlib import Path
import hashlib,json,struct,zlib
ROOT=Path(__file__).resolve().parents[2]
VM=ROOT/'build/m10a2-p6-gui-run-04'
raw=(VM/'p6-gui.json').read_bytes();report=json.loads(raw)
actions=report['p6_gui_runtime']['actions']
sha=lambda b:hashlib.sha256(b).hexdigest()
def actual(ident):
    item=actions[ident];assert item['status']=='EXECUTED_EFFECT_REQUIRES_EVIDENCE'
    row=item['result'];body=Path(row['path']).read_bytes()
    assert row['bytes']==len(body) and row['sha256']==sha(body)
    return row,body
bundles=[]
for name,prefix,journal_id in (
    ('RDSTATS.SCL','gui4-rdstats','gui4-rdstats-publication-actual-journal-get'),
    ('RDESK.SCX','gui4-rdesk','gui4-rdesk-publication-journal-get')):
    image,body=actual(prefix+('-native-library-get' if name.endswith('SCL') else '-native-app-get'))
    assert body[:8]==(b'SCL1MIO\0' if name.endswith('SCL') else b'SCX1MIO\0')
    mapping,_=actual(prefix+'-native-map-get')
    debug,_=actual(prefix+'-native-debug-get')
    outputs=[debug,mapping,image]
    receipt,data=actual(journal_id)
    assert len(data)==1344 and data[:8]==b'SCSFJ1\0\0'
    h=struct.unpack_from('<14I',data,8)
    assert h[:2]==(64,1) and h[6:9]==(1000,1000,0) and zlib.crc32(data[:60])==h[13]
    frames=[]
    for offset in range(64,len(data),256):
        size,version,kind,sequence,count,checksum=struct.unpack_from('<6I',data,offset)
        assert (size,version,count,sequence)==(256,1,232,len(frames)+1)
        value=data[offset+24:offset+size]
        assert zlib.crc32(value,zlib.crc32(data[offset:offset+20]))==checksum
        row=struct.unpack_from('<24I',value)
        assert row[:2]==(232,1) and row[5]==1 and row[22:24]==(0,0)
        frame=dict(kind=kind,item=row[4],state=row[6],stage=row[7],
            status=struct.unpack_from('<i',value,32)[0],flags=row[19],
            bytes=row[12],bytes_hi=row[13],destination_revision=row[17],
            source=value[96:164].split(b'\0',1)[0].decode(),
            destination=value[164:232].split(b'\0',1)[0].decode())
        frames.append(frame)
    assert frames[0]['kind']==0x100 and frames[-1]['kind']==0x101
    for i,frame in enumerate(frames[1:4]):
        output=outputs[i]
        assert (frame['item'],frame['state'],frame['stage'],frame['status'])==(i+1,1,3,0)
        assert frame['flags']&1 and not frame['bytes_hi']
        assert frame['bytes']==output['bytes'] and frame['destination']==output['guest']
        assert frame['destination_revision']==output['generation']
    bundles.append(dict(name=name,outputs=outputs,journal=receipt,header_crc=True,frame_crcs=True,frames=frames))
result=dict(status='ACTUAL_STUDIO_SCL_AND_FOUR_TU_SCX_PUBLICATION_DURABLE',
    report_at_observation_sha256=sha(raw),report_still_live=report['status']=='RUNNING',
    bundles=bundles,root_viewed_link_and_publication_screens=True,
    app_run_observed=True,debug_workflow_verified=False,full_M10a2=False,
    new_guest_execution=False)
path=VM/'gui4-studio-publication-audit.json';assert not path.exists()
path.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(result['status'])
