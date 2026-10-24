"""Confere (ou regrava, com --update) tests/golden/m7_menus.json a partir dos resumos de scripts/compare_shots.py e
scripts/compare_audio.py gravados pelo scripts/verify_m7.sh.

    python3 -I scripts/check_m7_golden.py <logdir> --pad-script <roteiro> --frames N [--update]

Em <logdir>: compare_<arch>_f<quadro>.json (capturas contra o Dolphin), audio_<arch>.json (áudio contra o Dolphin) e
runtime_<arch>.wav (o --audio-dump). O golden guarda o roteiro, as métricas e o SHA-256 dos pixels de cada captura e
das amostras do áudio da runtime, nunca a imagem nem o som (PADROES_DE_CODIGO.md §8). Os builds (arm64, x86_64) têm de
dar exatamente os mesmos pixels e as mesmas amostras.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GOLDEN = ROOT / "tests" / "golden" / "m7_menus.json"
SHOT_KEYS = ("psnr_db", "bad_pixels_pct", "max_abs", "lit_runtime_pct", "lit_dolphin_pct", "runtime_rgb_sha256", "ok")
AUDIO_KEYS = ("seconds_runtime", "lag_ms", "active", "silent_both", "presence_diverge", "pct_corr_ok", "pct_db_ok",
              "corr_median", "db_diff_median", "db_diff_max", "passed")


def pcm_sha256(path: Path) -> str:
    """SHA-256 das amostras (bloco data) do WAV da runtime."""
    data = path.read_bytes()
    pos = 12
    while pos + 8 <= len(data):
        size = int.from_bytes(data[pos + 4:pos + 8], "little")
        if data[pos:pos + 4] == b"data":
            return hashlib.sha256(data[pos + 8:pos + 8 + size]).hexdigest()
        pos += 8 + size
    raise SystemExit(f"{path}: sem bloco data")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("logdir")
    ap.add_argument("--pad-script", required=True)
    ap.add_argument("--frames", type=int, required=True)
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()
    logdir = Path(args.logdir)
    bad = 0
    shots: dict[str, dict] = {}
    audio: dict | None = None
    builds = set()
    for f in sorted(logdir.glob("compare_*_f*.json")):
        m = re.match(r"compare_(.+)_f(\d+)\.json$", f.name)
        if not m:
            continue
        builds.add(m.group(1))
        s = json.loads(f.read_text())
        cur = {k: s.get(k) for k in SHOT_KEYS}
        if not cur["ok"]:
            print(f"{f.name}: não passou no critério (ver a saída de compare_shots.py)")
            bad += 1
        prev = shots.get(m.group(2))
        if prev is not None and prev != cur:
            print(f"{f.name}: difere do outro build no quadro {m.group(2)}")
            bad += 1
        shots[m.group(2)] = cur
    for arch in sorted(builds):
        summary = json.loads((logdir / f"audio_{arch}.json").read_text())["summary"]
        cur = {k: summary.get(k) for k in AUDIO_KEYS}
        cur["runtime_pcm_sha256"] = pcm_sha256(logdir / f"runtime_{arch}.wav")
        if not cur["passed"]:
            print(f"audio_{arch}.json: não passou no critério (ver a saída de compare_audio.py)")
            bad += 1
        if audio is not None and audio != cur:
            print(f"audio_{arch}.json: difere do outro build")
            bad += 1
        audio = cur
    if not shots or audio is None:
        print(f"faltam resumos em {logdir}")
        return 1
    golden = {"dolphin": "2609a", "criterion": "docs/M7_MENUS.md §5", "pad_script": args.pad_script,
              "frames": args.frames, "shots": {k: shots[k] for k in sorted(shots, key=int)}, "audio": audio}
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
        print(f"igual ao golden ({len(builds)} build(s)): {desc}; áudio: correlação ok em {audio['pct_corr_ok']}%, "
              f"nível ok em {audio['pct_db_ok']}%")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
