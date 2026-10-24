"""Confere (ou regrava, com --update) tests/golden/m5_events.json a partir dos resumos de scripts/compare_events.py.

    python3 -I scripts/check_m5_golden.py <diretório com compare_*.json> --pad-script <roteiro> --frames N [--update]

O golden guarda o roteiro de controle, contagens e o SHA-256 da sequência estrita (módulos + leituras fora de fluxo),
nunca o log de eventos: deslocamentos e tamanhos de leitura são derivados do disco e ficam em logs/, fora do
repositório (PADROES_DE_CODIGO.md §8). Todos os builds têm de dar o mesmo resumo.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GOLDEN = ROOT / "tests" / "golden" / "m5_events.json"
KEYS = ("strict_compared", "first_divergence", "module_events", "strict_reads", "stream_files", "stream_reads",
        "stream_reads_identical", "stream_bad_segments", "module_frames_equal", "strict_sha256", "ok")


def pick(s: dict) -> dict:
    cur = {k: s.get(k) for k in KEYS}
    for side in ("dolphin", "runtime"):
        cur[f"{side}_link_frame"] = s[side]["link_frame"]
        cur[f"{side}_frames_after_link"] = s[side]["frames_after_link"]
        cur[f"{side}_events"] = s[side]["events"]
    return cur


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("logdir")
    ap.add_argument("--pad-script", required=True)
    ap.add_argument("--frames", type=int, required=True)
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()
    files = sorted(Path(args.logdir).glob("compare_*.json"))
    if not files:
        print(f"nenhum compare_*.json em {args.logdir}")
        return 1
    bad = 0
    result = None
    for f in files:
        cur = pick(json.loads(f.read_text()))
        if not cur["ok"]:
            print(f"{f.name}: não passou no critério (ver a saída de compare_events.py)")
            bad += 1
        if result is not None and cur != result:
            print(f"{f.name}: difere de outro build")
            bad += 1
        result = cur
    golden = {"dolphin": "2609a", "criterion": "docs/M5_OS_DISCO.md §5", "pad_script": args.pad_script,
              "frames": args.frames, "summary": result}
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
        s = result
        print(f"igual ao golden ({len(files)} build(s)): {s['strict_compared']} eventos estritos, "
              f"{s['stream_reads']} leituras de ADX; sha256 {s['strict_sha256'][:16]}…")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
