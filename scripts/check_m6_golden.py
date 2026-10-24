"""Confere (ou regrava, com --update) tests/golden/m6_frame.json a partir dos resumos de scripts/compare_shots.py.

    python3 -I scripts/check_m6_golden.py <diretório com compare_<arch>_f<quadro>.json> --pad-script <roteiro>
        --frames N [--update]

O golden guarda o roteiro, os quadros capturados, as métricas contra o Dolphin (PSNR, pixels ruins, pixels acesos) e
o SHA-256 dos pixels RGB da captura da runtime, nunca a imagem (PADROES_DE_CODIGO.md §8). Os dois builds (arm64,
x86_64) têm de dar exatamente os mesmos pixels: o GX em software compila sem contração de FMA.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GOLDEN = ROOT / "tests" / "golden" / "m6_frame.json"
KEYS = ("psnr_db", "bad_pixels_pct", "max_abs", "lit_runtime_pct", "lit_dolphin_pct", "runtime_rgb_sha256", "ok")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("logdir")
    ap.add_argument("--pad-script", required=True)
    ap.add_argument("--frames", type=int, required=True)
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()
    files = sorted(Path(args.logdir).glob("compare_*_f*.json"))
    if not files:
        print(f"nenhum compare_*_f*.json em {args.logdir}")
        return 1
    bad = 0
    by_frame: dict[str, dict] = {}
    builds = set()
    for f in files:
        m = re.match(r"compare_(.+)_f(\d+)\.json$", f.name)
        if not m:
            continue
        builds.add(m.group(1))
        s = json.loads(f.read_text())
        cur = {k: s.get(k) for k in KEYS}
        if not cur["ok"]:
            print(f"{f.name}: não passou no critério (ver a saída de compare_shots.py)")
            bad += 1
        prev = by_frame.get(m.group(2))
        if prev is not None and prev != cur:
            print(f"{f.name}: difere do outro build no quadro {m.group(2)}")
            bad += 1
        by_frame[m.group(2)] = cur
    golden = {"dolphin": "2609a", "criterion": "docs/M6_PRIMEIRO_QUADRO.md §5", "pad_script": args.pad_script,
              "frames": args.frames, "shots": {k: by_frame[k] for k in sorted(by_frame, key=int)}}
    if args.update:
        GOLDEN.write_text(json.dumps(golden, indent=1, ensure_ascii=False) + "\n")
        print(f"golden regravado: {GOLDEN.relative_to(ROOT)}")
        return 1 if bad else 0
    want = json.loads(GOLDEN.read_text())
    if want != golden:
        for k in sorted(set(want) | set(golden)):
            if want.get(k) != golden.get(k):
                print(f"golden difere em {k}: esperado {want.get(k)}, obtido {golden.get(k)}")
        bad += 1
    else:
        desc = ", ".join(f"q{k} {v['psnr_db']} dB" for k, v in golden["shots"].items())
        print(f"igual ao golden ({len(builds)} build(s)): {desc}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
