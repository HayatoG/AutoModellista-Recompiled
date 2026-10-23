"""Compara o estado no entry point (registradores + MEM1 inteira) da runtime com o do Dolphin (docs/M4_BOOT.md §3).

    python3 -I scripts/compare_boot_state.py <prefixo_dolphin> <prefixo_runtime>

Cada prefixo tem <prefixo>.json (registradores) e <prefixo>.mem1.bin (24 MiB), gravados por
`scripts/dolphin_trace.py boot-state` e `automodellista --dump-boot-state`. Diferenças conhecidas e explicadas estão
em EXPECTED_*; qualquer outra faz o script sair com 1.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

BASE = 0x80000000

# Faixas de MEM1 que diferem por motivo documentado (início inclusivo, fim exclusivo, motivo).
EXPECTED_MEM = [
    (0x80001800, 0x80001810, "Dolphin HLE: 'STUBHAXX' em 0x80001804 (HLE::PatchFixedFunctions, HBReload); não existe "
                             "no hardware"),
    (0x800030D4, 0x800030E0, "0x30D4: escrito pelo apploader (stw em 0x81200C08), nenhum acesso no DOL, não "
                             "reproduzido; 0x30D8: TB preset = segundos desde 2000 x 40,5 MHz, Dolphin usa o relógio "
                             "do host e a runtime usa --rtc (padrão 0) para ser determinística"),
    (0x81200000, 0x81300010, "código, dados e argumentos do apploader (apploader.img carregado em 0x81200000 e "
                             "0x81300000); a runtime não roda o apploader; região vira arena e o OSInit limpa"),
    (0x815664E0, 0x81566550, "pilha do apploader abaixo do r1 inicial (0x81566550)"),
]

# Registradores com valor de sobra do apploader no Dolphin (a runtime zera).
EXPECTED_REGS = {
    "r4": "sobra do apploader", "r5": "sobra do apploader", "r6": "sobra do apploader", "r7": "sobra do apploader",
    "r12": "sobra do apploader", "cr": "sobra do apploader", "srr0": "sobra do apploader (último rfi)",
    "srr1": "sobra do apploader",
}


def diff_ranges(a: bytes, b: bytes) -> list[tuple[int, int]]:
    out = []
    i = 0
    n = min(len(a), len(b))
    while i < n:
        if a[i] != b[i]:
            j = i
            while j < n and (a[j] != b[j] or a[j:j + 16] != b[j:j + 16]):
                j += 1
            out.append((BASE + i, BASE + j))
            i = j
        else:
            i += 1
    return out


def explain(lo: int, hi: int) -> str | None:
    for s, e, why in EXPECTED_MEM:
        if lo >= s and hi <= e:
            return why
    return None


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    dp, rp = sys.argv[1], sys.argv[2]
    dregs = json.loads(Path(dp + ".json").read_text())["registers"]
    rregs = json.loads(Path(rp + ".json").read_text())["registers"]
    bad = 0
    for k, v in dregs.items():
        rv = rregs.get(k)
        if rv == v:
            continue
        why = EXPECTED_REGS.get(k)
        print(f"reg {k:6s} Dolphin {v}  runtime {rv}  -> {why or 'NÃO EXPLICADO'}")
        bad += why is None
    dm = Path(dp + ".mem1.bin").read_bytes()
    rm = Path(rp + ".mem1.bin").read_bytes()
    ranges = diff_ranges(dm, rm)
    total = sum(e - s for s, e in ranges)
    for s, e in ranges:
        why = explain(s, e)
        if why is None:
            print(f"mem {s:08X}-{e:08X} ({e - s} bytes) -> NÃO EXPLICADO")
            bad += 1
    print(f"MEM1: {len(ranges)} faixa(s) diferentes, {total} bytes; {len(ranges) - bad} explicadas")
    for s, e, why in EXPECTED_MEM:
        print(f"  {s:08X}-{e:08X}: {why}")
    print("OK: estado inicial igual ao do Dolphin a menos das diferenças documentadas" if bad == 0
          else f"FALHA: {bad} diferença(s) sem explicação")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
