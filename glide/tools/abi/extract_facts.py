#!/usr/bin/env python3
# Provenance tool: turns Glide 2.x interface facts (constant names and
# values, function prototypes) from the 1999 source release into the data
# tables abi/constants.tsv and a prototype list. Only names and numbers are
# emitted; no header text is reproduced (PRD D12). The release itself is
# never stored in this repository.
#   usage: extract_facts.py <glide-release-root> <constants.tsv> <protos.tsv>
import re,sys
root=sys.argv.pop(1).rstrip('/')+'/'
src=root+'glide2x/sst1/glide/src/'
files=[src+'glide.h', src+'glideutl.h', src+'gump.h', root+'glide2x/sst1/init/sst1vid.h']
vals={}; order=[]
def ev(expr):
    e=re.sub(r'/\*.*?\*/','',expr).strip()
    e=re.sub(r'\b(0x[0-9A-Fa-f]+|\d+)[UuLl]+\b',r'\1',e)
    e=re.sub(r'\(\s*Fx[IU]\d+\s*\)','',e)
    e=re.sub(r'\b([A-Za-z_][A-Za-z0-9_]*)\b',lambda m: str(vals[m.group(1)]) if m.group(1) in vals else m.group(1),e)
    e=re.sub(r'FXBIT\(\s*(\d+)\s*\)',r'(1<<\1)',e)
    if re.search(r'[G-Zg-z_]',re.sub(r'0[xX][0-9A-Fa-f]+','0',e)): return None
    try: return int(eval(e,{},{}))
    except Exception: return None
for f in files:
    txt=open(f).read()
    txt=re.sub(r'\\\n',' ',txt)
    for m in re.finditer(r'^\s*#\s*define\s+((?:GR|GU|GLIDE|SST_)[A-Za-z0-9_]*)\s+(.+)$',txt,re.M):
        n,v=m.group(1),m.group(2)
        x=ev(v)
        if x is None: continue
        if n not in vals: order.append((n,f.split('/')[-1]))
        vals[n]=x
with open(sys.argv[1],'w') as o:
    o.write('# name\tvalue\tsource-header (Glide 2.x interface fact)\n')
    for n,f in order: o.write(f'{n}\t{vals[n]:#x}\t{f}\n')
# prototypes
protos=[]
for f in files[:2]:
    txt=open(f).read()
    txt=re.sub(r'/\*.*?\*/','',txt,flags=re.S)
    for m in re.finditer(r'FX_ENTRY\s+(.*?)\s+FX_CALL\s+(\w+)\s*\((.*?)\)\s*;',txt,re.S):
        ret,name,args=m.group(1),m.group(2),' '.join(m.group(3).split())
        protos.append((name,' '.join(ret.split()),args))
with open(sys.argv[2],'w') as o:
    for n,r,a in protos: o.write(f'{n}\t{r}\t{a}\n')
print(len(order),'constants',len(protos),'prototypes')
