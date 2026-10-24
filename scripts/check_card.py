"""Confere (ou regrava, com --update) tests/golden/m8_card.json a partir do que scripts/verify_card.sh gravou.

    python3 -I scripts/check_card.py <logdir> [--dolphin <cartão do Dolphin>] [--update]

Em <logdir>, para cada arquitetura <arch>: card_save_<arch>.raw (cartão depois de salvar), card_load_<arch>.raw (depois
da leitura), card_{save,load}_<arch>.log (a linha "memory card:" do relatório) e as capturas save_card_<arch>.png e
load_card_<arch>.png. O golden guarda só contagens e SHA-256 (nunca o cartão nem as imagens).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_shots  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
GOLDEN = ROOT / "tests" / "golden" / "m8_card.json"
BLOCK = 0x2000


def directory(card: bytes) -> dict:
    """Arquivos do diretório ativo (o de maior contador) e blocos livres da BAT ativa."""
    def counter(b: int) -> int:
        return struct.unpack(">h", card[b * BLOCK + 0x1FFA:b * BLOCK + 0x1FFC])[0]
    d = 1 if (counter(1) - counter(2)) % 65536 < 32768 else 2
    files = []
    for i in range(127):
        e = card[d * BLOCK + i * 64:d * BLOCK + (i + 1) * 64]
        if e[:4] != b"\xff\xff\xff\xff":
            files.append({"blocks": struct.unpack(">H", e[0x38:0x3A])[0]})
    bat = [struct.unpack(">h", card[b * BLOCK + 4:b * BLOCK + 6])[0] for b in (3, 4)]
    b = 3 if (bat[0] - bat[1]) % 65536 < 32768 else 4
    return {"files": files, "free_blocks": struct.unpack(">H", card[b * BLOCK + 6:b * BLOCK + 8])[0]}


def stats(log: Path) -> dict:
    m = re.search(r"memory card: (\d+) comandos; lido (\d+) bytes em (\d+) DMA\(s\) e (\d+) byte\(s\) avulsos; gravado "
                  r"(\d+) bytes em (\d+) DMA\(s\) e (\d+) PageProgram \((\d+) bytes por escrita imediata\); (\d+) setores apagados",
                  log.read_text())
    if not m:
        raise SystemExit(f"{log}: sem a linha 'memory card:' do relatório")
    keys = ("commands", "dma_read_bytes", "dma_reads", "array_bytes", "dma_write_bytes", "dma_writes",
            "page_programs", "program_bytes", "sector_erases")
    return dict(zip(keys, map(int, m.groups())))


def shot_sha(path: Path) -> str:
    return hashlib.sha256(compare_shots.read_png(path)[2]).hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("logdir")
    ap.add_argument("--dolphin", default=None)
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()
    logdir = Path(args.logdir)
    cur: dict = {}
    bad = 0
    for arch in ("arm64", "x86_64"):
        if not (logdir / f"card_save_{arch}.raw").exists():
            continue
        saved = (logdir / f"card_save_{arch}.raw").read_bytes()
        loaded = (logdir / f"card_load_{arch}.raw").read_bytes()
        one = {
            "saved_card_sha256": hashlib.sha256(saved).hexdigest(),
            "saved": directory(saved),
            "save_run": stats(logdir / f"card_save_{arch}.log"),
            "load_run": stats(logdir / f"card_load_{arch}.log"),
            "saved_screen_sha256": shot_sha(logdir / f"save_card_{arch}.png"),
            "loaded_screen_sha256": shot_sha(logdir / f"load_card_{arch}.png"),
        }
        # A segunda execução não pode ter perdido o save: o arquivo do jogo continua no cartão, com o mesmo tamanho.
        if directory(loaded)["files"] != one["saved"]["files"]:
            print(f"{arch}: o cartão depois da leitura perdeu o arquivo do jogo")
            bad += 1
        if one["load_run"]["dma_read_bytes"] < BLOCK:
            print(f"{arch}: a execução de leitura não leu os dados do cartão")
            bad += 1
        if cur and cur != one:
            print(f"{arch}: resultado diferente do outro build")
            bad += 1
        cur = one
    if not cur:
        print("nenhum resultado em", logdir)
        return 1
    if args.dolphin:
        ours = (logdir / "card_save_arm64.raw").read_bytes()
        theirs = Path(args.dolphin).read_bytes()
        diff = [i for i in range(min(len(ours), len(theirs))) if ours[i] != theirs[i]]
        blocks = sorted({i // BLOCK for i in diff})
        cur["dolphin"] = {"same_size": len(ours) == len(theirs), "bytes_differ": len(diff), "blocks_differ": blocks,
                          "identical": not diff and len(ours) == len(theirs)}
        print(f"Dolphin: {len(diff)} byte(s) diferentes, blocos {blocks}")
    if args.update:
        GOLDEN.write_text(json.dumps(cur, indent=1) + "\n")
        print("golden regravado:", GOLDEN)
        return 0 if not bad else 1
    gold = json.loads(GOLDEN.read_text())
    gold_cmp = {k: v for k, v in gold.items() if k != "dolphin"}
    cur_cmp = {k: v for k, v in cur.items() if k != "dolphin"}
    if gold_cmp != cur_cmp:
        for k in sorted(set(gold_cmp) | set(cur_cmp)):
            if gold_cmp.get(k) != cur_cmp.get(k):
                print(f"golden: {k} mudou\n  golden: {gold_cmp.get(k)}\n  agora:  {cur_cmp.get(k)}")
        bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
