"""Agrupa endereços (ex.: relatório de SMC do DolRecomp) pela função que os contém, usando um symbols.txt do dtk.

    python3 -I scripts/addr2func.py <symbols.txt> <arquivo com 0xADDR ou 0xA-0xB por linha>
"""

from __future__ import annotations

import bisect
import collections
import re
import sys

SYM = re.compile(r"^(\S+) = \.(\w+):0x([0-9A-Fa-f]+); // type:(\w+)(?: size:0x([0-9A-Fa-f]+))?")


def load(path: str) -> list[tuple[int, int, str]]:
    funcs = []
    for line in open(path):
        m = SYM.match(line)
        if m and m.group(4) == "function":
            funcs.append((int(m.group(3), 16), int(m.group(5) or "4", 16), m.group(1)))
    return sorted(funcs)


def main() -> None:
    funcs = load(sys.argv[1])
    starts = [f[0] for f in funcs]
    groups = collections.Counter()
    for line in open(sys.argv[2]):
        m = re.match(r"0x([0-9A-Fa-f]+)", line.strip())
        if not m:
            continue
        addr = int(m.group(1), 16)
        i = bisect.bisect_right(starts, addr) - 1
        if i >= 0 and addr < funcs[i][0] + funcs[i][1]:
            groups[f"{funcs[i][2]} @ {funcs[i][0]:08X}"] += 1
        else:
            groups["(fora de função conhecida)"] += 1
    for name, n in groups.most_common():
        print(f"{n:5}  {name}")


if __name__ == "__main__":
    main()
