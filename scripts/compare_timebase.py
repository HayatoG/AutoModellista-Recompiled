"""M8: compara o tempo emulado da runtime com o do Dolphin quadro a quadro (arquivos do --timebase-out dos dois lados:
"quadro timebase", "read ...", "dvdirq ...", "probe ..."; o timebase é o do último mftb do jogo, que é o que o stub
GDB do Dolphin mostra).

    python3 -I scripts/compare_timebase.py runtime_tb.txt dolphin_tb.txt [--frame 6400] [--max-lag 3] [--json r.json]

Defasagem = (timebase do Dolphin - timebase da runtime) na entrada do quadro, em campos de VI NTSC (40,5 MHz / 59,94).
Critério: |defasagem| no --frame <= --max-lag campos. Mostra os degraus (quadros em que a defasagem muda mais que 0,3
campo), que é onde procurar a causa.
"""

from __future__ import annotations

import argparse
import json
import sys

FIELD_TICKS = 40500000 / 59.94


def frames(path: str) -> dict[int, int]:
    out: dict[int, int] = {}
    for line in open(path):
        t = line.split()
        if t and t[0].isdigit():
            out[int(t[0])] = int(t[1])
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("runtime")
    ap.add_argument("dolphin")
    ap.add_argument("--frame", type=int, default=6400)
    ap.add_argument("--max-lag", type=float, default=3.0)
    ap.add_argument("--json", default=None)
    args = ap.parse_args()
    r = frames(args.runtime)
    d = frames(args.dolphin)
    common = sorted(set(r) & set(d))
    if args.frame not in r or args.frame not in d:
        print(f"quadro {args.frame} ausente (runtime {max(r, default=0)}, Dolphin {max(d, default=0)} quadros)")
        return 1
    lag = {q: (d[q] - r[q]) / FIELD_TICKS for q in common}
    steps = []
    prev = None
    for q in common:
        if prev is not None and abs(lag[q] - prev) > 0.3:
            steps.append({"frame": q, "lag_fields": round(lag[q], 3)})
        prev = lag[q]
    at = lag[args.frame]
    summary = {"frame": args.frame, "frames_compared": len(common), "lag_fields": round(at, 3),
               "lag_seconds": round(at / 59.94, 4), "lag_first_frame_fields": round(lag[common[0]], 3),
               "max_abs_lag_fields": round(max(abs(v) for v in lag.values()), 3), "steps": steps[:20],
               "ok": abs(at) <= args.max_lag}
    print(f"defasagem no q{args.frame}: {at:.3f} campo(s) ({at / 59.94:.4f} s); no q{common[0]}: "
          f"{lag[common[0]]:.3f}; degraus: " + (", ".join(f"q{s['frame']} {s['lag_fields']}" for s in steps[:8]) or
                                               "nenhum") + (" -> OK" if summary["ok"] else " -> FALHOU"))
    if args.json:
        with open(args.json, "w") as f:
            json.dump(summary, f, indent=1)
    return 0 if summary["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
