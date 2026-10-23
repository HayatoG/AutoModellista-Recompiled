"""Soma o uso de tokens e o intervalo de tempo de cada sessão/subagente do Claude Code (para os roteiros).

    python3 -I scripts/usage_report.py "<glob de .jsonl em ~/.claude/projects>" [...]

Cada resposta conta uma vez (pelo id da mensagem). Horários em UTC.
"""
import json, sys, glob, os, collections
from datetime import datetime
def parse(path):
    seen=set(); t=collections.Counter(); models=collections.Counter(); first=last=None; n=0
    for line in open(path, errors="ignore"):
        try: e=json.loads(line)
        except: continue
        ts=e.get("timestamp")
        if ts:
            d=datetime.fromisoformat(ts.replace("Z","+00:00"))
            first=first or d; last=d
        m=e.get("message") or {}
        u=m.get("usage") if isinstance(m,dict) else None
        if not u: continue
        mid=m.get("id") or e.get("uuid")
        if mid in seen: continue
        seen.add(mid); n+=1
        for k in ("input_tokens","output_tokens","cache_creation_input_tokens","cache_read_input_tokens"):
            t[k]+=u.get(k,0) or 0
        models[m.get("model","?")]+=1
    return t,models,first,last,n
rows=[]
for p in sys.argv[1:]:
    for f in sorted(glob.glob(p, recursive=True)):
        t,mo,a,b,n=parse(f)
        if not n: continue
        rows.append((f,t,mo,a,b,n))
for f,t,mo,a,b,n in rows:
    tot=sum(t.values())
    print(f"{os.path.relpath(f, os.path.expanduser('~/.claude/projects'))[:95]}")
    print(f"   {a:%d/%m %H:%M}→{b:%d/%m %H:%M} UTC  resp={n}  in={t['input_tokens']:,} out={t['output_tokens']:,} cache_w={t['cache_creation_input_tokens']:,} cache_r={t['cache_read_input_tokens']:,} total={tot:,}  {dict(mo.most_common(2))}")
