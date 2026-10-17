"""暂停期间只读真实R3 allocator/cache；不向客体内存写入。"""
from pathlib import Path
import hashlib,json,re,struct,time

def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def layout(root,basis):
    names=('magic','version','arena_size','block_size','context_glyphs','cache_first','cache_count',
           'cache_budget','cache_used','entry_size','entry_next','entry_key','entry_key_bytes',
           'entry_resource','entry_valid','key_size','glyph_size')
    directory=root/'build/m10a2-parent-heap-layout-01'
    data=(directory/'layout.bin').read_bytes()
    values=dict(zip(names,struct.unpack('<'+'I'*len(names),data)))
    assert values['magic']==0x48454150 and values['version']==1
    assert values['arena_size']==values['block_size']==32 and values['key_size']==16 and values['glyph_size']==1600
    sources={}
    groups={'SCCLIB':('user/stdlib/memory.c','user/stdlib/SCCLIBPRIVATE.H'),
            'SCLIB':('user/sclib/SCLIBPRIVATE.H','user/sclib/sclib_hash.c','user/sclib/sclib_cache.c'),
            'SCUI':('user/scui/SCUIPRIVATE.H',)}
    for label,items in groups.items():
        closure=basis.runtime_native['preflight']['source_closures'][label]
        for name in items:
            assert digest(root/name)==closure[name],name
            sources[name]=closure[name]
    return dict(values=values,sources=sources,layout_bin_sha256=digest(directory/'layout.bin'),
                layout_o_sha256=digest(directory/'layout.o'),source_sha256=digest(Path(__file__).with_name('layout.c')))

def take(harness,owner,core,proof,basis,ready):
    shot=ready['snapshot'];host=shot['host'];pid=host['pid'];pd=next(r['pd'] for r in shot['owners'] if r['pid']==pid)
    source=basis.runtime_native['native_libraries']['SCCLIB.SCL']
    mapping=Path(source['path']+'.map');text=mapping.read_text(encoding='utf-8')
    offsets={}
    for name in ('__scc_arenas','__scc_warm_arena'):
        matches=re.findall(r'^OBJECT ([0-9A-Fa-f]+) 4 '+name+r' object=0 binding=local$',text,re.M)
        assert len(matches)==1,name
        offsets[name]=int(matches[0],16)
    objects=owner.elf_objects(core/'core.elf');v=proof['values'];started=time.monotonic()
    result=dict(status='UNVERIFIED',guest_memory_writes=False,pid=pid,generation=host['generation'],
                allocator_image_sha256=source['sha256'],allocator_map_sha256=digest(mapping),layout=proof)
    assert harness.vm.qmp('query-status')['running']
    try:
        harness.vm.qmp('stop');read=owner.PausedReader(harness.vm,20)
        matches=[]
        for key,at,stride in read.pool(objects['sc32_debug.c::d2_modules']['address']):
            assert stride==384
            words=read.words(at,2);info=read.words(at+180,16)
            if words==[pid,host['generation']] and read.physical(at+244,32).hex()==source['sha256']:
                assert info[:2]==[204,1] and info[4]==2 and info[11]&7==5
                matches.append(info[5])
        assert len(matches)==1,'actual SCCLIB owner/base not unique'
        base=matches[0]
        def word(address):return struct.unpack('<I',read.virtual(pd,address,4))[0]
        head=word(base+offsets['__scc_arenas']);warm=word(base+offsets['__scc_warm_arena'])
        arenas=[];seen=set();blocks={};previous=0
        while head:
            assert 0x40000000<=head<0x44000000 and not head&4095 and head not in seen
            seen.add(head);a=struct.unpack('<8I',read.virtual(pd,head,32));next_,prev,bytes_,live,dedicated,*reserved=a
            assert prev==previous and bytes_>=4096 and not bytes_&4095 and dedicated<=1 and not any(reserved)
            at=head+32;end=head+bytes_;last_span=0;allocated=0;free_bytes=0;rows=[]
            while at<end:
                span,prior,requested,arena,nfree,pfree,magic,used=struct.unpack('<8I',read.virtual(pd,at,32))
                assert magic==0x53434342 and arena==head and prior==last_span and used<=1
                assert span>=48 and not span&15 and at+span<=end and (requested<=span-32 if used else requested==0)
                block=dict(header=at,payload=at+32,span=span,requested=requested,allocated=bool(used),arena=head)
                rows.append(block);blocks[at+32]=block;allocated+=used
                if not used:free_bytes+=span-32
                at+=span;last_span=span
            assert at==end and allocated==live
            arenas.append(dict(address=head,bytes=bytes_,live=live,dedicated=dedicated,warm=head==warm,
                               free_payload_bytes=free_bytes,blocks=rows))
            previous,head=head,next_
        assert not warm or warm in seen
        result.update(arenas=arenas,arena_count=len(arenas),arena_pages=sum(r['bytes']//4096 for r in arenas),warm=warm)
        ui=word(basis_binding(ready)['desk']+basis.layout['values']['desk_ui'])
        cache=word(ui+v['context_glyphs']);first=word(cache+v['cache_first']);glyphs=[];entries=set();cost=0
        while first:
            assert first not in entries;entries.add(first)
            key=word(first+v['entry_key']);size=word(first+v['entry_key_bytes'])
            resource=word(first+v['entry_resource']);valid=word(first+v['entry_valid'])
            assert size==16 and valid==1 and key in blocks and first in blocks and resource in blocks
            assert blocks[resource]['requested']==1600
            face,pixels,scalar,generation=struct.unpack('<4I',read.virtual(pd,key,16))
            glyphs.append(dict(entry=first,key=key,resource=resource,face=face,pixels=pixels,scalar=scalar,generation=generation))
            # SCLIB按资源+entry+含NUL键总量计费，不只计glyph正文。
            cost+=1600+v['entry_size']+size+1;first=word(first+v['entry_next'])
        assert len(glyphs)==word(cache+v['cache_count']),'glyph count differs'
        assert cost==word(cache+v['cache_used']),'cache charge differs'
        result.update(glyph_cache=dict(address=cache,count=len(glyphs),bytes=cost,resource_bytes=len(glyphs)*1600,budget=word(cache+v['cache_budget']),glyphs=glyphs),
                      status='ACTUAL_ALLOCATOR_BLOCKS_AND_GLYPH_CACHE_READ')
    finally:
        harness.vm.qmp('cont');result['resume_confirmed']=harness.vm.qmp('query-status')['running']
        result['paused_wall_seconds']=time.monotonic()-started
    return result

def basis_binding(ready):return ready['snapshot']['binding']
