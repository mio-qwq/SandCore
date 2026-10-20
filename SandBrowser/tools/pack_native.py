"""Copyright (c) 2026 mio. GNU ELF32 -> SCL；不修改平台格式或平台工具。"""
import struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.dont_write_bytecode=True
sys.path.insert(0,str(ROOT.parent/'sandcore/tools'))
import scl_format as scl

def pack(path,name,exports,imports):
 data=Path(path).read_bytes()
 if data[:7]!=b'\x7fELF\x01\x01\x01':raise ValueError('Expected ELF32 little endian')
 phoff,shoff=struct.unpack_from('<II',data,28)
 phsize,phnum,shsize,shnum,shstr=struct.unpack_from('<5H',data,42)
 sections=[struct.unpack_from('<10I',data,shoff+i*shsize) for i in range(shnum)]
 st=sections[shstr];strings=data[st[4]:st[4]+st[5]]
 def string(buf,p):return buf[p:buf.index(0,p)].decode()
 def sname(s):return string(strings,s[0])
 symbols={};tables={}
 for i,s in enumerate(sections):
  if s[1]!=2:continue
  names=sections[s[6]];ns=data[names[4]:names[4]+names[5]];table=[]
  for off in range(s[4],s[4]+s[5],s[9]):
   n,value,size,info,other,idx=struct.unpack_from('<IIIBBH',data,off)
   sym=(string(ns,n),value,size,idx);table.append(sym)
   if n and idx:symbols[sym[0]]=sym
   if n and not idx:raise ValueError('Unresolved native symbol: '+sym[0])
  tables[i]=table
 segments=[]
 for i in range(phnum):
  t,off,va,pa,files,mem,flags,align=struct.unpack_from('<8I',data,phoff+i*phsize)
  if t!=1 or not mem:continue
  if va&4095:raise ValueError('Unaligned segment')
  segments.append(dict(rva=va,mem_size=(mem+4095)&~4095,flags=(1 if flags&4 else 0)|(2 if flags&2 else 0)|(4 if flags&1 else 0),data=data[off:off+files]))
 def read_word(va):
  for s in sections:
   if s[1]!=8 and s[2]&2 and s[3]<=va and va+4<=s[3]+s[5]:return struct.unpack_from('<I',data,s[4]+va-s[3])[0]
  raise ValueError(f'Relocation not in initialized data at {va:x}')
 fix={};got=symbols.get('_GLOBAL_OFFSET_TABLE_',('',0,0,0))[1]
 got_sections=[s for s in sections if sname(s).startswith('.got')]
 for s in sections:
  if s[1]!=9 or not sections[s[7]][2]&2:continue
  for off in range(s[4],s[4]+s[5],s[9]):
   patch,info=struct.unpack_from('<II',data,off);typ=info&255;symbol=tables[s[6]][info>>8]
   if typ==1 and symbol[3]!=0xfff1:fix[patch]=read_word(patch)
   elif typ in (3,43):
    slot=(got+read_word(patch))&0xffffffff
    if any(g[3]<=slot and slot+4<=g[3]+g[5] for g in got_sections):
     val=read_word(slot)
     if val:fix[slot]=val
   elif typ not in (0,1,2,4,9,10):raise ValueError(f'Unsupported target relocation {typ} at {patch:x}')
 rel=[dict(patch_rva=p,type=1,target_kind=0,target=v,addend=0) for p,v in sorted(fix.items())]
 for i,item in enumerate(imports):
  slot=symbols['sb_slot_'+item['name']][1]
  rel=[r for r in rel if r['patch_rva']!=slot]
  rel.append(dict(patch_rva=slot,type=1,target_kind=1,target=i,addend=0))
 deps={}
 for item in imports:deps[item['dependency']]=dict(name=item['dependency'],abi_major=1,min_minor=0,min_version=1)
 out=scl.build_scl(name=name,abi_major=1,abi_minor=0,library_version=1,
  segments=segments,exports=[dict(name=n,rva=symbols[n][1],size=max(1,symbols[n][2]),kind=1) for n in exports],
  imports=[dict(name=x['name'],dependency=x['dependency'],kind=1,size=1) for x in imports],
  dependencies=list(deps.values()),relocations=rel,init_rva=symbols['SbRuntimeInit'][1],fini_rva=symbols['SbRuntimeFini'][1])
 return out
