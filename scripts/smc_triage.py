"""Triagem dos alertas de SMC do DolRecomp: para cada store sinalizado, acha a última instrução que definiu o
registrador-base (varredura linear para trás, como a heurística do DolRecomp). Se foi algo que a heurística não
rastreia (load, add, mr, bl...), o "endereço conhecido" é obsoleto e o alerta é falso positivo.

    python3 -I scripts/smc_triage.py <disassembly objdump> <smc_addrs.txt (hex sem 0x)>
"""

from __future__ import annotations

import collections
import re
import sys

LINE = re.compile(r"^\s*([0-9a-f]+):\t(?:[0-9a-f]{2} ){4}\t(\S+)\s*(.*)$")
STORE_BASE = re.compile(r"(-?\w+)\((r\d+)\)|^(?:\d+|r\d+),(r\d+),(r\d+)")
TRACKED = {"lis", "li", "addi", "addis", "ori", "oris", "xori"}


def dest(mnem: str, ops: str) -> str | None:
    if mnem.startswith(("st", "cmp", "b", "dc", "ic", "mt", "sync", "isync", "nop", "tw", "crx", "cror", "psq_st")):
        return None if not mnem.startswith(("stwu", "stbu", "sthu")) else None
    first = ops.split(",")[0].strip()
    return first if re.fullmatch(r"r\d+", first) else None


def main() -> None:
    insts = []
    for line in open(sys.argv[1]):
        m = LINE.match(line)
        if m:
            insts.append((int(m.group(1), 16), m.group(2), m.group(3)))
    index = {a: i for i, (a, _, _) in enumerate(insts)}
    flagged = [int(x, 16) for x in open(sys.argv[2]).read().split()]
    verdict = collections.Counter()
    examples = collections.defaultdict(list)
    for addr in flagged:
        i = index.get(addr)
        if i is None:
            verdict["fora do disassembly"] += 1
            continue
        _, mnem, ops = insts[i]
        if mnem == "icbi":
            verdict["icbi (manutenção de cache: real)"] += 1
            continue
        m = re.search(r"\((r\d+)\)", ops)
        base = m.group(1) if m else ops.split(",")[1].strip()
        for j in range(i - 1, max(-1, i - 4000), -1):
            _, pm, po = insts[j]
            if pm in ("blr", "rfi", "b") or (pm == "bctr"):
                key = f"base {base} vem de antes do início da função (argumento/herdado → falso positivo)"
                break
            if pm in ("bl", "blrl", "bctrl") and base in {f"r{n}" for n in (0, *range(3, 13))}:
                key = f"base {base} clobbered por chamada ({pm})"
                break
            if dest(pm, po) == base:
                key = f"base rastreada: {pm} {po}" if pm in TRACKED else f"base redefinida por {pm} (não rastreado → falso positivo)"
                break
        else:
            key = "definição não encontrada"
        verdict[key] += 1
        if len(examples[key]) < 3:
            examples[key].append(f"{addr:08X} {mnem} {ops}")
    for k, n in verdict.most_common():
        print(f"{n:5}  {k}")
        for e in examples.get(k, []):
            print(f"         ex: {e}")


if __name__ == "__main__":
    main()
