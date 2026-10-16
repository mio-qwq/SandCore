"""核本轮真实get正文/REC和已见自动刷新画面；不接客体、不改原报告。"""
from pathlib import Path
import hashlib,json,struct,zlib
ROOT=Path(__file__).resolve().parents[2]
VM=ROOT/'build/m10a2-p6-gui-run-03'
raw=(VM/'p6-gui.json').read_bytes();report=json.loads(raw)
actions=report['p6_gui_runtime']['actions']
sha=lambda b:hashlib.sha256(b).hexdigest()

def actual(ident):
    a=actions[ident];assert a['status']=='EXECUTED_EFFECT_REQUIRES_EVIDENCE'
    row=a['result'];p=Path(row['path']);body=p.read_bytes()
    assert row['sha256']==sha(body) and len(body)==row['bytes']
    return row,body

rows=[]
for ident,name,op,source,target in (
    ('gui3-copy-journal-read','gui3-desktop-copy.rec',1,'/HOME/P6GUI/NOTE.TXT','/DESK/NOTE.TXT'),
    ('gui3-move-journal-read','gui3-desktop-move.rec',2,'/DESK/NOTE.TXT','/HOME/P6GUI/BACK/NOTE.TXT')):
    get,body=actual(ident)
    assert Path(get['path']).name==name and len(body)==832 and body[:8]==b'SCSFJ1\0\0'
    h=struct.unpack_from('<14I',body,8)
    assert h[:2]==(64,1) and h[6:9]==(1000,1000,0) and zlib.crc32(body[:60])==h[13]
    frames=[]
    for offset in (64,320,576):
        size,version,kind,sequence,count,checksum=struct.unpack_from('<6I',body,offset)
        assert (size,version,count,sequence)==(256,1,232,len(frames)+1)
        value=body[offset+24:offset+size]
        assert zlib.crc32(value,zlib.crc32(body[offset:offset+20]))==checksum
        row=struct.unpack_from('<24I',value)
        assert row[:2]==(232,1) and row[5]==op and row[22:24]==(0,0)
        status=struct.unpack_from('<i',value,32)[0]
        frames.append(dict(kind=kind,sequence=sequence,state=row[6],stage=row[7],status=status,flags=row[19],
            bytes_lo=row[12],bytes_hi=row[13],worker_pid=row[20],worker_generation=row[21],
            source=value[96:164].split(b'\0',1)[0].decode(),destination=value[164:232].split(b'\0',1)[0].decode()))
    assert frames[0]['kind']==0x100 and frames[-1]['kind']==0x101
    c=frames[1]
    assert (c['state'],c['stage'],c['status'],c['bytes_lo'],c['bytes_hi'])==(1,3 if op==1 else 4,0,20,0)
    assert c['flags']&1 and c['source']==source and c['destination']==target
    rows.append(dict(file=name,sha256=sha(body),header_crc=True,frame_crcs=True,frames=frames))
bodies=[]
for ident in ('gui3-note-saved-body','gui3-desktop-copied-body','gui3-moved-note-observed'):
    row,body=actual(ident);assert body==b'P6 native GUI note.\n';bodies.append(row)
screen=actions['gui3-move-auto-refresh-screen']['result'];assert sha(Path(screen['path']).read_bytes())==screen['sha256']
listed=actions['gui3-after-move-desk-list']['result'];assert listed['exit_code']==0
assert 'NOTE.TXT' not in listed['output'].splitlines()
result=dict(status='ACTUAL_DEFERRED_DROP_COPY_MOVE_DURABLE_AND_AUTOMATIC_SOURCE_REFRESH_OBSERVED',
    original_report_at_observation_sha256=sha(raw),report_still_live=report['status']=='RUNNING',full_M10a2=False,
    journals=rows,bodies=bodies,source_absence_list=listed,automatic_source_refresh_screen=screen,
    root_visually_verified_no_F5=True,business_FINAL_wire_bytes_directly_captured=False,
    final_move_action_and_source_change_supported_by='ACTUAL_MOVE_JOURNAL_STAGE4_AND_SOURCE_VIEW_INVALIDATION_PATH',
    new_guest_execution=False)
out=VM/'gui3-drop-audit.json';assert not out.exists();out.write_bytes((json.dumps(result,ensure_ascii=False,indent=2)+'\n').encode())
print(result['status'])
