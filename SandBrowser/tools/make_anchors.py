"""Copyright (c) 2026 mio. 从已保存的公开证书快照提取 BearSSL 根锚。"""
from pathlib import Path
import base64,hashlib,json,re
ROOT=Path(__file__).resolve().parents[1]

def tlvs(data):
 p=0;out=[]
 while p<len(data):
  start=p;tag=data[p];p+=1;n=data[p];p+=1
  if n&128:
   size=n&127
   if not size or size>4:raise ValueError('Bad DER length')
   n=int.from_bytes(data[p:p+size],'big');p+=size
  if p+n>len(data):raise ValueError('Truncated DER')
  out.append((tag,data[p:p+n],data[start:p+n]));p+=n
 return out

def main():
 policy=(ROOT/'resources/trust/root_store.textproto').read_text()
 active={};p=0
 while True:
  m=re.search(r'(?m)^trust_anchors\s*\{',policy[p:])
  if not m:break
  start=p+m.start();i=p+m.end();depth=1
  while depth and i<len(policy):
   if policy[i]=='{':depth+=1
   elif policy[i]=='}':depth-=1
   i+=1
  block=policy[start:i];p=i
  digest=re.search(r'sha256_hex:\s*"([0-9a-f]+)"',block)
  if digest:active[digest.group(1)]=block
 pem=(ROOT/'resources/trust/root_store.certs').read_text();certs=[]
 for body in re.findall(r'-----BEGIN CERTIFICATE-----\s*(.*?)-----END CERTIFICATE-----',pem,re.S):
  der=base64.b64decode(body);digest=hashlib.sha256(der).hexdigest()
  if digest not in active:continue
  # 没有实现 Chromium 专属的根约束时，保守排除该根，而非忽略约束。
  if 'constraints:' in active[digest] or 'constraints {' in active[digest]:continue
  tbs=tlvs(tlvs(tlvs(der)[0][1])[0][1]);tbs=tbs[1:] if tbs[0][0]==0xa0 else tbs
  subject=tbs[4][2];spki=tlvs(tbs[5][1]);algorithm=tlvs(spki[0][1]);key=spki[1][1][1:]
  if algorithm[0][1]==bytes.fromhex('2a864886f70d010101'):
   integers=tlvs(tlvs(key)[0][1]);a=integers[0][1].lstrip(b'\0');b=integers[1][1].lstrip(b'\0');kind=1
  elif algorithm[0][1]==bytes.fromhex('2a8648ce3d0201'):
   curve={bytes.fromhex('2a8648ce3d030107'):23,bytes.fromhex('2b81040022'):24,bytes.fromhex('2b81040023'):25}.get(algorithm[1][1])
   if not curve:continue
   a=key;b=curve;kind=2
  else:continue
  certs.append((digest,subject,kind,a,b))
 lines=['/* Generated from a pinned Chromium certificate snapshot. See resources/trust. */']
 def arr(name,data):lines.append('static const unsigned char '+name+'[]={'+','.join(str(x) for x in data)+'};')
 for i,(_,dn,kind,a,b) in enumerate(certs):
  arr('sb_ca_dn_'+str(i),dn);arr('sb_ca_a_'+str(i),a)
  if kind==1:arr('sb_ca_b_'+str(i),b)
 lines.append('static const br_x509_trust_anchor sb_anchors[]={')
 for i,(_,dn,kind,a,b) in enumerate(certs):
  prefix='{{(unsigned char*)sb_ca_dn_%d,%d},BR_X509_TA_CA,'%(i,len(dn))
  if kind==1:lines.append(prefix+'{BR_KEYTYPE_RSA,{.rsa={(unsigned char*)sb_ca_a_%d,%d,(unsigned char*)sb_ca_b_%d,%d}}}},'%(i,len(a),i,len(b)))
  else:lines.append(prefix+'{BR_KEYTYPE_EC,{.ec={%d,(unsigned char*)sb_ca_a_%d,%d}}}},'%(b,i,len(a)))
 lines+=['};','#define SB_ANCHOR_COUNT (sizeof(sb_anchors)/sizeof(sb_anchors[0]))']
 target=ROOT/'build/generated/anchors.h';target.parent.mkdir(parents=True,exist_ok=True);target.write_text('\n'.join(lines)+'\n')
 report=dict(source='Chromium Chrome Root Store snapshot',active=len(active),accepted=len(certs),excluded_policy_or_algorithm=len(active)-len(certs),certificate_sha256=[x[0] for x in certs],policy_sha256=hashlib.sha256(policy.encode()).hexdigest())
 (ROOT/'build/anchors.json').write_text(json.dumps(report,indent=2))
 if not certs:raise ValueError('No usable unconstrained trust anchors')
 print('Trust anchors:',len(certs),'of',len(active),'active roots; constrained/unsupported roots excluded')

if __name__=='__main__':main()
