from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[2]
for name in ('STUDIO2.JSON','STUDIO2.NATIVE.JSON'):
    p=ROOT/'user/studio2'/name;m=json.loads(p.read_bytes());names={r['name'] for r in m['imports']}
    for symbol in ('ScUiScrollQuery','ScUiScrollTo'):
        if symbol not in names:m['imports'].append(dict(name=symbol,dependency='SCUI.SCL',kind='function',size=1))
    m['imports'].sort(key=lambda r:r['name']);p.write_text(json.dumps(m,indent=2)+'\n',encoding='utf-8')
for p in (ROOT/'user/files2').glob('*.inc'):
    s=p.read_text(encoding='utf-8');new=s.replace(' — ',' - ').replace(' · ',' | ')
    if s!=new:p.write_text(new,encoding='utf-8')
p=ROOT/'docs/M10A2-SDK.md';s=p.read_text(encoding='utf-8').replace('SCUI为ABI1.4/v7','SCUI为ABI1.4/v8');p.write_text(s,encoding='utf-8')
s=(Path(__file__).with_name('build_host24.py')).read_text(encoding='utf-8')
s=s.replace("old=ROOT/'build/m10a2-main-product-fix-01'","old=ROOT/'build/m10a2-main-product-fix-02'")
s=s.replace("new=ROOT/'build/m10a2-main-product-fix-02'","new=ROOT/'build/m10a2-main-product-fix-03'")
s=s.replace("b'build/m10a2-main-product-fix-01',b'build/m10a2-main-product-fix-02'","b'build/m10a2-main-product-fix-02',b'build/m10a2-main-product-fix-03'")
s=s.replace('p6-unified-host-24','p6-unified-host-25')
Path(__file__).with_name('build_host25.py').write_text(s,encoding='utf-8')
print('VISIBLE_PRODUCT_ISSUES_FIXED_HOST25_PREPARED')
