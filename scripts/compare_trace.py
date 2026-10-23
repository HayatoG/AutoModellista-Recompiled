"""Compara o trace de chamadas da runtime com o do Dolphin até a primeira divergência (docs/M4_BOOT.md §5).

    python3 -I scripts/compare_trace.py --dol <main.dol> --dolphin <dolphin_trace_raw.txt> --runtime <runtime_trace.txt>
            [--summary-json <arquivo>]

Normalização, igual para os dois lados:
  1. só eventos que são CHAMADA (am_trace_lib.is_call: a instrução em LR-4 é um desvio com link para o pc);
  2. só alvos que o Dolphin consegue ver (o conjunto de breakpoints gravado em <dolphin>.breakpoints);
  2b. só chamadas entre TRECHOS diferentes do C gerado (tabela <runtime>.chunks): o DolRecomp transforma um bl
     dentro do mesmo trecho de 16 KiB num goto local, que nunca volta ao chassi e por isso não aparece no trace da
     runtime. Para comparar a mesma coisa, o mesmo filtro estático é aplicado ao trace do Dolphin;
  3. repetições consecutivas do mesmo evento (pc, lr) viram um evento só: o número de voltas de laços de espera
     (ex.: OSGetTick em __OSInitAudioSystem) depende do tempo emulado, que não é igual nos dois. As contagens
     aparecem no relatório, mas não entram na comparação;
  4. compara-se (pc, lr); o CTR fica fora (só serve para conferir bcctrl).
Sai com 0 se as sequências normalizadas são iguais e as duas terminam em main; 1 caso contrário.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from am_trace_lib import Dol, chunk_of, is_call, load_chunks, load_function_names, read_trace  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MAIN = 0x800719AC


def normalize(events, read32, allowed, chunks):
    calls = [e for e in events if is_call(read32, *e)]
    outside = [e for e in calls if e[0] not in allowed]
    kept = [(pc, lr) for pc, lr, _ in calls
            if pc in allowed and (chunks is None or chunk_of(chunks, pc) != chunk_of(chunks, lr - 4))]
    collapsed: list[tuple[tuple[int, int], int]] = []
    for ev in kept:
        if collapsed and collapsed[-1][0] == ev:
            collapsed[-1] = (ev, collapsed[-1][1] + 1)
        else:
            collapsed.append((ev, 1))
    return calls, outside, collapsed


def seq_hash(events) -> str:
    """SHA-256 da sequência normalizada (pc, lr), sem as contagens de repetição: vai para tests/golden/."""
    text = "".join(f"{pc:08X} {lr:08X}\n" for (pc, lr), _ in events)
    return hashlib.sha256(text.encode()).hexdigest()


def owner_name(names: dict[int, str], starts: list[int], addr: int) -> str:
    import bisect

    i = bisect.bisect_right(starts, addr) - 1
    if i < 0:
        return "?"
    return f"{names[starts[i]]}+0x{addr - starts[i]:X}"


def describe(names, starts, ev):
    (pc, lr), n = ev
    return f"{names.get(pc, f'fn_{pc:08X}'):28s} {pc:08X}  de {owner_name(names, starts, lr - 4):34s} x{n}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dol", required=True)
    ap.add_argument("--dolphin", required=True)
    ap.add_argument("--runtime", required=True)
    ap.add_argument("--call-graph", default=str(ROOT / "analysis" / "call_graph.json"))
    ap.add_argument("--chunks", default=None, help="tabela de trechos (padrão: <runtime>.chunks)")
    ap.add_argument("--all-calls", action="store_true", help="não filtra por trecho (mostra o que a runtime não vê)")
    ap.add_argument("--context", type=int, default=6)
    ap.add_argument("--summary-json", default=None)
    args = ap.parse_args()

    dol = Dol(Path(args.dol))
    names = load_function_names(Path(args.call_graph))
    starts = sorted(names)
    allowed = {int(x, 16) for x in Path(args.dolphin + ".breakpoints").read_text().split()}

    chunks = None if args.all_calls else load_chunks(Path(args.chunks or args.runtime + ".chunks"))

    dt = read_trace(Path(args.dolphin))
    rt = read_trace(Path(args.runtime))
    d_calls, _, d = normalize(dt.events, dol.read32, allowed, chunks)
    r_calls, r_out, r = normalize(rt.events, dol.read32, allowed, chunks)
    if chunks is not None:
        print(f"filtro: chamadas entre trechos diferentes ({len(chunks)} trechos no C gerado)")

    print(f"Dolphin : {len(dt.events)} paradas, {len(d_calls)} chamadas, {len(d)} eventos normalizados "
          f"(fim: {dt.header.get('end', '?')})")
    print(f"runtime : {len(rt.events)} eventos, {len(r_calls)} chamadas, {len(r)} eventos normalizados; "
          f"{len(r_out)} chamada(s) a alvos fora dos breakpoints")
    for pc, lr, _ in r_out[:5]:
        print(f"  fora do conjunto: {pc:08X} de {owner_name(names, starts, lr - 4)}")

    first = None
    for i in range(min(len(d), len(r))):
        if d[i][0] != r[i][0]:
            first = i
            break
    if first is None and len(d) != len(r):
        first = min(len(d), len(r))

    repeats = [(i, d[i][1], r[i][1]) for i in range(min(len(d), len(r)) if first is None else first)
               if d[i][1] != r[i][1]]
    for i, nd, nr in repeats[:10]:
        print(f"  repetição diferente no evento {i}: {describe(names, starts, d[i])} | runtime x{nr}")

    d_main = bool(d) and d[-1][0][0] == MAIN
    r_main = bool(r) and r[-1][0][0] == MAIN
    summary = {
        "dolphin_events": len(d),
        "runtime_events": len(r),
        "first_divergence": first,
        "both_end_at_main": d_main and r_main,
        "repeat_count_differences": len(repeats),
        "runtime_calls_outside_breakpoints": len(r_out),
        "chunks": len(chunks) if chunks is not None else None,
        "dolphin_calls": len(d_calls),
        "dolphin_sha256": seq_hash(d),
        "runtime_sha256": seq_hash(r),
        "dolphin_header": dt.header,
    }
    if first is None:
        print(f"IGUAIS: {len(d)} eventos normalizados idênticos; os dois terminam em main: {d_main and r_main}")
    else:
        print(f"DIVERGÊNCIA no evento normalizado {first}:")
        lo = max(0, first - args.context)
        for i in range(lo, first + args.context):
            dl = describe(names, starts, d[i]) if i < len(d) else "(fim)"
            rl = describe(names, starts, r[i]) if i < len(r) else "(fim)"
            mark = ">>" if i == first else "  "
            print(f"{mark} {i:5d} D: {dl}")
            print(f"{mark} {'':5s} R: {rl}")
    if args.summary_json:
        Path(args.summary_json).write_text(json.dumps(summary, indent=1) + "\n")
    return 0 if first is None and d_main and r_main else 1


if __name__ == "__main__":
    sys.exit(main())
