"""只在新目录接线GUI03后续，原driver/来源/收账检查原件保持。"""
from pathlib import Path
import ast
import difflib
import hashlib
import json
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
old=ROOT/'drafts/m10a2-p6-gui-coherent/driver.py'
text=old.read_text(encoding='utf-8')
text=text.replace("HERE.parent/'m10a2-p6-gui-runtime', HERE.parent/'m10a2-p6-final-flow'):",
    "HERE.parent/'m10a2-p6-gui-runtime', HERE.parent/'m10a2-p6-final-flow',\n                  HERE.parent/'m10a2-p6-gui-coherent'):")
text=text.replace('from coherent_runtime import CoherentRuntime, ObserverPlan',
    'from coherent_runtime import CoherentRuntime, ObserverPlan\nfrom heartbeat_guest import FollowupGuest')
text=text.replace('guest = GuiGuest(vm)','guest = FollowupGuest(vm)')
text=text.replace("HERE.parent/'m10a2-gui-owner-review'):",
    "HERE.parent/'m10a2-gui-owner-review', HERE.parent/'m10a2-p6-gui-coherent'):")
assert 'guest = FollowupGuest(vm)' in text
ast.parse(text)
(HERE/'driver.py').write_text(text,encoding='utf-8')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(HERE/'driver.patch').write_text(''.join(difflib.unified_diff(old.read_text(encoding='utf-8').splitlines(True),text.splitlines(True),fromfile='frozen_gui_driver.py',tofile='new_gui_driver.py')),encoding='utf-8')
(HERE/'PREPARATION.json').write_text(json.dumps(dict(status='NEW_GUI_DRIVER_PREPARED_NOT_EXECUTED',
    frozen_driver_sha256=sha(old),driver_sha256=sha(HERE/'driver.py'),
    heartbeat_sha256=sha(HERE/'heartbeat_guest.py'),
    old_helpers_modified=False,guest_memory_writes=False,forced_finish=False,
    diagnostic_only_of_host_transport=True),indent=2)+'\n')
print('NEW_GUI_DRIVER_PREPARED_NOT_EXECUTED')
