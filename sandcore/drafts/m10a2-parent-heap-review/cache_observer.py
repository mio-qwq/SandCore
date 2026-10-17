"""同一次暂停读取allocator、字形所有权和PTE；只归属逐块证明的时钟缓存。"""
from pathlib import Path
import inspect,re,struct
import survey

def configure(owner,basis,proof):
    source=basis.runtime_native['native_libraries']['SCCLIB.SCL']
    mapping=Path(source['path']+'.map');body=mapping.read_text(encoding='utf-8')
    offsets={}
    for name in ('__scc_arenas','__scc_warm_arena'):
        matches=re.findall(r'^OBJECT ([0-9A-Fa-f]+) 4 '+name+r' object=0 binding=local$',body,re.M)
        assert len(matches)==1;offsets[name]=int(matches[0],16)
    def census(read,objects,host,ui):
        pd,pid,generation=host['pd'],host['pid'],host['generation'];v=proof['values']
        def word(address):return struct.unpack('<I',read.virtual(pd,address,4))[0]
        matches=[]
        for _,at,stride in read.pool(objects['sc32_debug.c::d2_modules']['address']):
            assert stride==384
            words=read.words(at,2);info=read.words(at+180,16)
            if words==[pid,generation] and read.physical(at+244,32).hex()==source['sha256']:
                assert info[:2]==[204,1] and info[4]==2 and info[11]&7==5;matches.append(info[5])
        assert len(matches)==1
        base=matches[0];head=word(base+offsets['__scc_arenas']);warm=word(base+offsets['__scc_warm_arena'])
        arenas=[];seen=set();blocks={};previous=0
        while head:
            assert 0x40000000<=head<0x44000000 and not head&4095 and head not in seen
            seen.add(head);a=struct.unpack('<8I',read.virtual(pd,head,32))
            next_,prev,size,live,dedicated,*reserved=a
            assert prev==previous and size>=4096 and not size&4095 and dedicated<=1 and not any(reserved)
            at=head+32;end=head+size;last_span=0;allocated=0;rows=[];frames=[]
            for va in range(head,end,4096):
                pde=read.words(pd+(va>>22)*4)[0];assert pde&0x85==5
                pte=read.words((pde&~4095)+((va>>12)&1023)*4)[0]
                assert pte&5==5 and not pte&0x800;frames.append(pte&~4095)
            while at<end:
                span,prior,requested,arena,_,_,magic,used=struct.unpack('<8I',read.virtual(pd,at,32))
                assert magic==0x53434342 and arena==head and prior==last_span and used<=1
                assert span>=48 and not span&15 and at+span<=end and (requested<=span-32 if used else requested==0)
                row=dict(payload=at+32,span=span,requested=requested,allocated=bool(used),arena=head)
                rows.append(row);blocks[at+32]=row;allocated+=used;at+=span;last_span=span
            assert at==end and live==allocated and len(set(frames))==len(frames)
            arenas.append(dict(address=head,bytes=size,live=live,dedicated=dedicated,frames=frames,blocks=rows))
            previous,head=head,next_
        assert not warm or warm in seen
        cache=word(ui+v['context_glyphs']);first=word(cache+v['cache_first']);glyphs=[];entries=set();cost=0
        while first:
            assert first not in entries;entries.add(first)
            key=word(first+v['entry_key']);resource=word(first+v['entry_resource']);size=word(first+v['entry_key_bytes'])
            assert size==16 and word(first+v['entry_valid'])==1
            assert all(p in blocks and blocks[p]['allocated'] for p in (first,key,resource))
            assert blocks[resource]['requested']==1600
            face,pixels,scalar,gen=struct.unpack('<4I',read.virtual(pd,key,16))
            glyphs.append(dict(entry=first,key=key,resource=resource,face=face,pixels=pixels,scalar=scalar,generation=gen))
            cost+=1600+v['entry_size']+size+1;first=word(first+v['entry_next'])
        budget=word(cache+v['cache_budget'])
        assert len(glyphs)==word(cache+v['cache_count']) and cost==word(cache+v['cache_used']) and cost<=budget
        return dict(status='ACTUAL_SAME_PAUSE_ALLOCATOR_CACHE_AND_PTE',guest_memory_writes=False,
            pid=pid,generation=generation,allocator_image_sha256=source['sha256'],allocator_map_sha256=survey.digest(mapping),
            layout=proof,arenas=arenas,warm=warm,glyphs=glyphs,budget=budget,charged_bytes=cost)
    # 固定旧观察器保持原字节。新函数只在其完整暂停中插入一次只读普查，
    # PF、全部owner、资源池和缓存的结果因此来自同一停止区间。
    text=inspect.getsource(owner.snapshot)
    anchor='result["resources"] = harness.resources()'
    assert text.count(anchor)==1
    text=text.replace(anchor,anchor+'\n            result["resources"]["user_clock_cache"] = clock_census(read, objects, host, ui)\n            result["resources"]["owner_census"] = result["owners"]')
    namespace=dict(vars(owner),clock_census=census)
    exec(compile(text,str(Path(__file__).resolve())+'::same_pause','exec'),namespace)
    exec(compile(inspect.getsource(owner.wait_original_host),str(Path(__file__).resolve())+'::wait','exec'),namespace)
    return namespace['wait_original_host']

def account(ledger,before,after):
    """不能用固定16页宽限：必须逐项等于真实字形块及新增PTE。"""
    a,b=before['user_clock_cache'],after['user_clock_cache']
    assert a['status']==b['status']=='ACTUAL_SAME_PAUSE_ALLOCATOR_CACHE_AND_PTE'
    assert (a['pid'],a['generation'],a['allocator_image_sha256'],a['layout'])==(b['pid'],b['generation'],b['allocator_image_sha256'],b['layout'])
    def ownership(value):return {g[k]:g for g in value['glyphs'] for k in ('entry','key','resource')}
    ao,bo=ownership(a),ownership(b)
    def nonglyph(value,owned):return {r['payload']:r for ar in value['arenas'] for r in ar['blocks'] if r['allocated'] and r['payload'] not in owned}
    assert nonglyph(a,ao)==nonglyph(b,bo),'non-glyph desktop allocation changed'
    aa={ar['address']:ar for ar in a['arenas']};bb={ar['address']:ar for ar in b['arenas']}
    assert aa.keys()<=bb.keys(),'original arena removed'
    for address,ar in aa.items():
        assert ar['frames']==bb[address]['frames'] and ar['bytes']==bb[address]['bytes']
    oldkeys={(g['face'],g['pixels'],g['scalar'],g['generation']) for g in a['glyphs']}
    newglyphs=[g for g in b['glyphs'] if (g['face'],g['pixels'],g['scalar'],g['generation']) not in oldkeys]
    assert all(g['face']==12 and g['pixels']==18 and 48<=g['scalar']<=57 for g in newglyphs)
    assert len(newglyphs)<=10 and b['charged_bytes']<=b['budget']==a['budget']
    newarenas=[ar for address,ar in bb.items() if address not in aa]
    for ar in newarenas:
        assert ar['bytes']==65536 and ar['dedicated']==0 and ar['live']>0
        for r in ar['blocks']:
            if r['allocated']:
                assert r['payload'] in bo and bo[r['payload']] in newglyphs,'new arena contains non-clock live allocation'
    frames={f for ar in newarenas for f in ar['frames']}
    assert len(frames)==sum(ar['bytes']//4096 for ar in newarenas)
    owners_a={(o['pid'],o['generation']):o for o in before['owner_census'] if o['pid']}
    owners_b={(o['pid'],o['generation']):o for o in after['owner_census'] if o['pid']}
    assert owners_a.keys()==owners_b.keys()
    identity=(a['pid'],a['generation'])
    for key,o in owners_a.items():
        p,q=o['pages'],owners_b[key]['pages']
        if key!=identity:assert p==q,'another owner mapping changed';continue
        assert set(q['private_frames'])-set(p['private_frames'])==frames
        assert not set(p['private_frames'])-set(q['private_frames'])
        for name in ('image_stack','modules','shared_window','shared_frames','page_tables'):
            assert p[name]==q[name],'non-heap mapping changed'
        assert q['heap']['mapped_pages']-p['heap']['mapped_pages']==len(frames)
    result=dict(ledger);result['actual_user_clock_glyph_cache_pages']=len(frames)
    result['user_clock_cache_proof']=dict(before=a,after=b,new_glyphs=newglyphs,new_arenas=newarenas,
        same_pause_owner_read=True,blanket_allowance=False,original_native07_reclassified=False)
    result['attributed_physical_delta']+=len(frames);result['unexplained_delta']-=len(frames)
    return result,not result['unexpected'] and result['unexplained_delta']==0
