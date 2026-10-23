"""Confere (ou regrava, com --update) tests/golden/m4_trace.json a partir dos resumos de scripts/compare_trace.py.

    python3 -I scripts/check_m4_golden.py <diretório com compare_*.json> [--update]

O golden guarda só metadados e hashes SHA-256 das sequências normalizadas (nunca o trace): o trace bruto é derivado
do jogo e fica em logs/ fora do repositório (PADROES_DE_CODIGO.md §8). Variante "default" = C gerado padrão (trechos de
4096 instruções, os quatro builds compartilham a mesma tabela); "c128" = scripts/regen_c128.sh.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GOLDEN = ROOT / "tests" / "golden" / "m4_trace.json"
KEYS = ("dolphin_events", "runtime_events", "chunks", "dolphin_sha256", "runtime_sha256", "first_divergence",
        "both_end_at_main")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    logdir = Path(sys.argv[1])
    update = "--update" in sys.argv[2:]
    variants: dict[str, dict] = {}
    bad = 0
    for f in sorted(logdir.glob("compare_*.json")):
        s = json.loads(f.read_text())
        name = "c128" if "c128" in f.name else "default"
        cur = {k: s.get(k) for k in KEYS}
        if cur["first_divergence"] is not None or not cur["both_end_at_main"]:
            print(f"{f.name}: divergência no evento {cur['first_divergence']} ou não chegou a main")
            bad += 1
        if name in variants and variants[name] != cur:
            print(f"{f.name}: difere de outro build da mesma variante")
            bad += 1
        variants[name] = cur
    if not variants:
        print(f"nenhum compare_*.json em {logdir}")
        return 1
    if update:
        old = json.loads(GOLDEN.read_text()) if GOLDEN.exists() else {}
        old.update({"dolphin": "2609a", "criterion": "docs/M4_BOOT.md §5", "variants": {
            **old.get("variants", {}), **variants}})
        GOLDEN.parent.mkdir(parents=True, exist_ok=True)
        GOLDEN.write_text(json.dumps(old, indent=1, ensure_ascii=False) + "\n")
        print(f"golden atualizado: {GOLDEN.relative_to(ROOT)} ({', '.join(variants)})")
        return 1 if bad else 0
    golden = json.loads(GOLDEN.read_text()).get("variants", {})
    for name, cur in variants.items():
        exp = golden.get(name)
        if exp is None:
            print(f"{name}: sem golden (rode com --update)")
            bad += 1
            continue
        diff = [k for k in KEYS if exp.get(k) != cur.get(k)]
        print(f"{name}: {'igual ao golden' if not diff else 'DIFERENTE em ' + ', '.join(diff)}"
              f" ({cur['runtime_events']} eventos, sha256 {cur['runtime_sha256'][:16]}…)")
        bad += bool(diff)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
