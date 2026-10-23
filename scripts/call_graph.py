"""Grafo de chamadas diretas (bl) do main.dol, a partir do disassembly do objdump e do symbols.txt do dtk.

    python3 -I scripts/call_graph.py <main.dis> <symbols.txt> <saída call_graph.json>

Só chamadas diretas: ponteiros de função e vtables (bctrl/blrl) não aparecem e são contados à parte.
"""

from __future__ import annotations

import bisect
import json
import re
import sys

LINE = re.compile(r"^\s*([0-9a-f]+):\t(?:[0-9a-f]{2} ){4}\t(\S+)\s*(.*)$")
SYM = re.compile(r"^(\S+) = \.(\w+):0x([0-9A-Fa-f]+); // type:function(?: size:0x([0-9A-Fa-f]+))?")


def main() -> None:
    funcs = []
    for line in open(sys.argv[2]):
        if m := SYM.match(line):
            funcs.append((int(m.group(3), 16), int(m.group(4) or "4", 16), m.group(1)))
    funcs.sort()
    starts = [f[0] for f in funcs]
    by_start = {f[0]: f for f in funcs}

    def owner(addr: int):
        i = bisect.bisect_right(starts, addr) - 1
        return funcs[i] if i >= 0 and addr < funcs[i][0] + funcs[i][1] else None

    nodes = {f[2]: {"addr": f"0x{f[0]:08X}", "size": f[1], "calls": set(), "callers": set(), "indirect": 0, "loops": 0}
             for f in funcs}
    for line in open(sys.argv[1]):
        m = LINE.match(line)
        if not m:
            continue
        addr, mnem, ops = int(m.group(1), 16), m.group(2), m.group(3)
        src = owner(addr)
        if not src:
            continue
        node = nodes[src[2]]
        if mnem == "bl" and (t := re.match(r"0x([0-9a-f]+)", ops)):
            dst = by_start.get(int(t.group(1), 16)) or owner(int(t.group(1), 16))
            if dst:
                node["calls"].add(dst[2])
                nodes[dst[2]]["callers"].add(src[2])
        elif mnem in ("bctrl", "blrl"):
            node["indirect"] += 1
        elif mnem.startswith("b") and (t := re.search(r"0x([0-9a-f]+)", ops)) and int(t.group(1), 16) < addr \
                and src[0] <= int(t.group(1), 16):
            node["loops"] += 1  # desvio para trás dentro da função
    out = {k: {**v, "calls": sorted(v["calls"]), "callers": sorted(v["callers"])} for k, v in nodes.items()}
    json.dump(out, open(sys.argv[3], "w"), indent=1)
    print(f"{len(out)} funções, {sum(len(v['calls']) for v in out.values())} arestas diretas")


if __name__ == "__main__":
    main()
