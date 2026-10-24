"""Confere (ou regrava, com --update) tests/golden/m8_corrida.json a partir dos resumos gravados por scripts/verify_m8.sh.

    python3 -I scripts/check_m8_golden.py <logdir> --pad-script <roteiro> --frames N [--update]

Em <logdir>:
  timebase_<arch>.json             scripts/compare_timebase.py (tempo emulado contra o Dolphin)
  soft_<arch>_f<q>.json            captura do GX em software contra o Dolphin (scripts/compare_shots.py)
  gpu_soft_<arch>_f<q>.json        captura da GPU contra a do software
  gpu_dolphin_<arch>_f<q>.json     captura da GPU contra o Dolphin
O golden guarda o tempo emulado e, do software, as métricas e o SHA-256 dos pixels (os dois builds têm de dar o mesmo);
da GPU, só se passou nos limiares (a imagem depende da GPU do computador). Nunca a imagem (PADROES_DE_CODIGO.md §8).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GOLDEN = ROOT / "tests" / "golden" / "m8_corrida.json"
SHOT_KEYS = ("psnr_db", "bad_pixels_pct", "max_abs", "runtime_rgb_sha256", "ok")
TB_KEYS = ("frame", "frames_compared", "lag_fields", "lag_first_frame_fields", "max_abs_lag_fields", "ok")


def load(path: Path) -> dict:
    return json.loads(path.read_text())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("logdir")
    ap.add_argument("--pad-script", required=True)
    ap.add_argument("--frames", type=int, required=True)
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()
    logdir = Path(args.logdir)
    bad = 0
    timebase = None
    for f in sorted(logdir.glob("timebase_*.json")):
        cur = {k: load(f).get(k) for k in TB_KEYS}
        if not cur["ok"]:
            print(f"{f.name}: defasagem acima do limite")
            bad += 1
        if timebase is not None and timebase != cur:
            print(f"{f.name}: tempo emulado difere do outro build")
            bad += 1
        timebase = cur
    soft: dict[str, dict] = {}
    gpu: dict[str, dict] = {}
    for f in sorted(logdir.glob("*_f*.json")):
        m = re.match(r"(soft|gpu_soft|gpu_dolphin)_(.+)_f(\d+)\.json$", f.name)
        if not m:
            continue
        kind, q = m.group(1), m.group(3)
        s = load(f)
        if not s.get("ok"):
            print(f"{f.name}: não passou no critério ({s.get('psnr_db')} dB, {s.get('bad_pixels_pct')}% ruins)")
            bad += 1
        if kind == "soft":
            cur = {k: s.get(k) for k in SHOT_KEYS}
            if q in soft and soft[q] != cur:
                print(f"{f.name}: o software difere do outro build no quadro {q}")
                bad += 1
            soft[q] = cur
        else:
            gpu.setdefault(q, {})[kind.replace("gpu_", "vs_") + "_ok"] = bool(s.get("ok"))
    if timebase is None or not soft:
        print(f"faltam resumos em {logdir}")
        return 1
    golden = {"dolphin": "2609a", "criterion": "docs/M8_CORRIDA.md §5", "pad_script": args.pad_script,
              "frames": args.frames, "timebase": timebase,
              "soft": {k: soft[k] for k in sorted(soft, key=int)}, "gpu": {k: gpu[k] for k in sorted(gpu, key=int)}}
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
        desc = ", ".join(f"q{k} {v['psnr_db']} dB" for k, v in golden["soft"].items())
        print(f"igual ao golden: defasagem {timebase['lag_fields']} campo(s) no q{timebase['frame']}; software contra o "
              f"Dolphin: {desc}; GPU dentro dos limiares em {len(gpu)} quadro(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
