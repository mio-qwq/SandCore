"""mio：独立复现Files还原，失败时只读保存布局现场，不改客体。"""
import json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import verify_files_matrix as m
stage=ROOT/'build/m8-files'
core=json.loads((stage/'results.json').read_text(encoding='utf-8'))
native=(stage/'files-native.scx').read_bytes()
mapping=(stage/'files-native.map').read_bytes()
addresses=m.theme.native_symbols(mapping)
m.OUT=ROOT/'build/m8-files-layout-observation'
old_shot=m.q.shot
def shot(label):
    if label=='failure':
        with m.t.stable_frame():
            w=m.theme.window()
            names=('UI_W','UI_H','ui_width','ui_height','ui_scale','ui_frames','ui_action','ui_modal',
                   'ui_compact','body_y','list_y','navigation_y','row_height','visible_rows','small_window','file_context')
            state={name:m.theme.user_word(w,addresses[name]) for name in names}
            state['window']=w;state['registers']=m.q.hmp('info registers')
            (m.v.OUT/'failure-state.json').write_text(json.dumps(state,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return old_shot(label)
m.q.shot=shot
result=m.run_case(640,480,200,'AURORA','std',native,mapping,core['inputs_sha256'])
print(json.dumps(result,ensure_ascii=False),flush=True)
