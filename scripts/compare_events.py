"""M5: compara o log de eventos da runtime (--events) com o do Dolphin (dolphin_trace.py events). docs/M5_OS_DISCO.md §6.

    python3 -I scripts/compare_events.py --dolphin <eventos> --runtime <eventos> [--disc /Volumes/AMRecomp/work/extracted]
                                         [--link-id 1] [--after 600] [--summary-json <arq>]

Eventos: `main`, `oslink`/`osunlink` (id do módulo e endereço real) e `di_read` (deslocamento, tamanho e destino da
leitura do DVD). O número do quadro fica fora da igualdade (é relatado à parte).

Leituras em fluxo: o jogo toca música (ADX da CRI) lendo pedaços de um mesmo arquivo de dentro de um .afs para um
buffer circular; o tamanho de cada pedaço é "o que já foi tocado", e isso depende de em que fração de quadro a CPU
olha a posição da voz do AX. Sem igualdade de ciclo de CPU com o Dolphin esses tamanhos variam em um ou dois setores.
Por isso a comparação tem dois níveis:
  1. ESTRITO, em ordem: main, OSLink/OSUnlink e toda leitura que não é de fluxo (campos inteiros, inclusive destino);
  2. FLUXO: entre dois eventos de módulo, o mesmo número de leituras de cada arquivo em fluxo nos dois lados; os
     bytes lidos são relatados (diferença esperada: poucos setores).
Arquivo em fluxo = entrada de um .afs (tabela do próprio .afs, lida de --disc/files) que é áudio ADX: começa com
0x80 0x00 e tem "(c)CRI" logo antes do início dos dados (cabeçalho do ADX). Só o cabeçalho é lido, para classificar.
As leituras são comparadas até o fim do lado mais curto (rode os dois com o mesmo --frames).
Critério de saída do M5: (1) igual, (2) mesma contagem, e os dois lados com o OSLink do módulo --link-id seguido de
pelo menos --after quadros. O resumo (--summary-json) guarda só contagens e hashes SHA-256: nada do jogo.
"""

from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import re
import struct
import sys
from collections import Counter
from pathlib import Path

FRAME = re.compile(r" frame=(\d+)")
HEADER_FRAMES = re.compile(r"\bframes=(\d+)")
READ = re.compile(r"offset=0x([0-9A-F]+) length=0x([0-9A-F]+)")


class DiscMap:
    """Deslocamento no disco -> (arquivo, entrada do .afs ou -1), pela FST e pelas tabelas dos .afs."""

    def __init__(self, root: Path) -> None:
        fst = (root / "sys" / "fst.bin").read_bytes()
        count = struct.unpack(">I", fst[8:12])[0]
        names = count * 12
        self.files: list[tuple[int, int, str]] = []

        def name(off: int) -> str:
            return fst[names + off: fst.index(b"\0", names + off)].decode("latin1")

        def walk(start: int, end: int, prefix: str) -> None:
            i = start
            while i < end:
                kind = fst[i * 12]
                name_off = int.from_bytes(fst[i * 12 + 1: i * 12 + 4], "big")
                a, b = struct.unpack(">II", fst[i * 12 + 4: i * 12 + 12])
                if kind == 1:
                    walk(i + 1, b, prefix + name(name_off) + "/")
                    i = b
                else:
                    self.files.append((a, b, prefix + name(name_off)))
                    i += 1

        walk(1, count, "")
        self.files.sort()
        self.starts = [f[0] for f in self.files]
        self.afs: dict[str, list[tuple[int, int]]] = {}
        self.adx: set[str] = set()
        for _, _, path in self.files:
            if path.endswith(".afs") and (root / "files" / path).exists():
                with open(root / "files" / path, "rb") as f:
                    head = f.read(8)
                    n = struct.unpack("<I", head[4:8])[0] if head[:4] == b"AFS\0" else 0
                    toc = f.read(n * 8)
                    entries = sorted(struct.unpack_from("<II", toc, i * 8) for i in range(n))
                    for j, (off, size) in enumerate(entries):
                        f.seek(off)
                        head = f.read(4)
                        if size >= 4 and head[:2] == b"\x80\x00":
                            data = int.from_bytes(head[2:4], "big")  # o ADX diz onde começam os dados (+4)
                            f.seek(off + data - 2)
                            if f.read(6) == b"(c)CRI":
                                self.adx.add(f"{path}#{j}")
                self.afs[path] = entries

    def key(self, offset: int) -> str:
        i = bisect.bisect_right(self.starts, offset) - 1
        if i < 0 or offset >= self.files[i][0] + self.files[i][1]:
            return "?"
        start, _, path = self.files[i]
        entries = self.afs.get(path)
        if not entries:
            return path
        rel = offset - start
        j = bisect.bisect_right([e[0] for e in entries], rel) - 1
        if j >= 0 and rel < entries[j][0] + entries[j][1]:
            return f"{path}#{j}"
        return path


def read_events(path: Path) -> tuple[list[str], list[int], int | None]:
    events: list[str] = []
    frames: list[int] = []
    total = None
    for line in path.read_text().splitlines():
        if line.startswith("#"):
            m = HEADER_FRAMES.search(line)
            total = int(m.group(1)) if m else total
            continue
        if not line.strip():
            continue
        m = FRAME.search(line)
        frames.append(int(m.group(1)) if m else 0)
        events.append(FRAME.sub("", line).strip())
    return events, frames, total


def sha(events: list[str]) -> str:
    return hashlib.sha256("".join(e + "\n" for e in events).encode()).hexdigest()


def classify(events: list[str], disc: DiscMap) -> list[str | None]:
    """Chave de arquivo de cada leitura (None para eventos de módulo)."""
    return [disc.key(int(READ.search(e).group(1), 16)) if e.startswith("di_read") else None for e in events]


def segments(events: list[str], keys: list[str | None], streams: set[str]) -> list[Counter]:
    """Por trecho entre eventos de módulo: (arquivo em fluxo) -> número de leituras; e bytes em '<arquivo>/bytes'."""
    out = [Counter()]
    for e, k in zip(events, keys):
        if k is None:
            out.append(Counter())
        elif k in streams:
            out[-1][k] += 1
            out[-1][k + "/bytes"] += int(READ.search(e).group(2), 16)
    return out


def stream_lists(events: list[str], keys: list[str | None], streams: set[str]) -> list[dict[str, list[str]]]:
    """Por trecho: (arquivo em fluxo) -> leituras em ordem, para contar quantas são idênticas nos dois lados."""
    out: list[dict[str, list[str]]] = [{}]
    for e, k in zip(events, keys):
        if k is None:
            out.append({})
        elif k in streams:
            out[-1].setdefault(k, []).append(e)
    return out


def link_info(events: list[str], frames: list[int], total: int | None, module_id: int, after: int) -> dict:
    idx = next((i for i, e in enumerate(events) if e.startswith("oslink ") and f" id={module_id} " in e + " "), None)
    info = {"events": len(events), "frames": total, "link_event": idx, "link_frame": None, "frames_after_link": None}
    if idx is not None:
        info["link_frame"] = frames[idx]
        info["frames_after_link"] = total - frames[idx] if total is not None else None
    info["ok"] = info["frames_after_link"] is not None and info["frames_after_link"] >= after
    return info


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dolphin", required=True)
    ap.add_argument("--runtime", required=True)
    ap.add_argument("--disc", default="/Volumes/AMRecomp/work/extracted", help="árvore extraída (sys/ e files/)")
    ap.add_argument("--link-id", type=int, default=1, help="módulo cujo OSLink marca o ponto de partida (1 = game.rel)")
    ap.add_argument("--after", type=int, default=600, help="quadros exigidos depois do OSLink")
    ap.add_argument("--context", type=int, default=4)
    ap.add_argument("--summary-json", default=None)
    args = ap.parse_args()

    disc = DiscMap(Path(args.disc))
    d, d_frames, d_total = read_events(Path(args.dolphin))
    r, r_frames, r_total = read_events(Path(args.runtime))
    d_info = link_info(d, d_frames, d_total, args.link_id, args.after)
    r_info = link_info(r, r_frames, r_total, args.link_id, args.after)
    for name, info in (("Dolphin", d_info), ("runtime", r_info)):
        print(f"{name:8}: {info['events']} eventos, {info['frames']} quadros; OSLink do módulo {args.link_id} no "
              f"quadro {info['link_frame']}, {info['frames_after_link']} quadros depois -> "
              f"{'OK' if info['ok'] else 'FALTA'}")

    # Até o fim do lado mais curto, contado em eventos de módulo + leituras que não são de fluxo.
    d_keys, r_keys = classify(d, disc), classify(r, disc)
    streams = disc.adx
    d_strict = [(i, e) for i, (e, k) in enumerate(zip(d, d_keys)) if k not in streams]
    r_strict = [(i, e) for i, (e, k) in enumerate(zip(r, r_keys)) if k not in streams]
    n = min(len(d_strict), len(r_strict))
    div = next((i for i in range(n) if d_strict[i][1] != r_strict[i][1]), None)
    ok = d_info["ok"] and r_info["ok"]
    if div is None:
        print(f"ESTRITO IGUAL: {n} eventos (módulos + leituras fora de fluxo) idênticos e na mesma ordem")
    else:
        ok = False
        print(f"ESTRITO DIVERGE no evento {div} de {n}:")
        for i in range(max(0, div - args.context), min(n, div + args.context + 1)):
            (di, de), (ri, re_) = d_strict[i], r_strict[i]
            print(f"{'>>' if i == div else '  '} {i:5}  Dolphin q{d_frames[di]:<5} {de:62} runtime q{r_frames[ri]:<5} {re_}")
    # O lado mais longo é cortado no mesmo evento estrito para a contagem de fluxo.
    d_cut = d_strict[n - 1][0] + 1 if n else 0
    r_cut = r_strict[n - 1][0] + 1 if n else 0
    d_seg = segments(d[:d_cut], d_keys[:d_cut], streams)
    r_seg = segments(r[:r_cut], r_keys[:r_cut], streams)
    stream_reads = sum(v for s in d_seg for k, v in s.items() if not k.endswith("/bytes"))
    d_lists = stream_lists(d[:d_cut], d_keys[:d_cut], streams)
    r_lists = stream_lists(r[:r_cut], r_keys[:r_cut], streams)
    n_files = len({k for k in d_keys[:d_cut] if k in streams})
    stream_same = sum(x == y for a, b in zip(d_lists, r_lists) for k in a for x, y in zip(a[k], b.get(k, [])))
    bad_segments = []
    for i, (a, b) in enumerate(zip(d_seg, r_seg)):
        ca = {k: v for k, v in a.items() if not k.endswith("/bytes")}
        cb = {k: v for k, v in b.items() if not k.endswith("/bytes")}
        if ca != cb:
            bad_segments.append(i)
            print(f"FLUXO DIVERGE no trecho {i}: Dolphin {ca} | runtime {cb}")
        elif ca:
            for k in sorted(ca):
                print(f"  trecho {i}: {k}: {ca[k]} leituras nos dois; bytes Dolphin 0x{a[k + '/bytes']:X}, runtime "
                      f"0x{b[k + '/bytes']:X}")
    if len(d_seg) != len(r_seg):
        bad_segments.append(-1)
    if not bad_segments:
        print(f"FLUXO IGUAL: {stream_reads} leituras de {n_files} arquivo(s) ADX, mesma contagem por trecho; "
              f"{stream_same} delas idênticas (deslocamento, tamanho e destino) na mesma posição do fluxo")
    ok = ok and not bad_segments
    module_events = [e for e in d[:d_cut] if not e.startswith("di_read")]
    d_mod_frames = [f for e, f in zip(d[:d_cut], d_frames) if not e.startswith("di_read")]
    r_mod_frames = [f for e, f in zip(r[:r_cut], r_frames) if not e.startswith("di_read")]
    print(f"eventos de módulo: {len(module_events)}; quadros iguais nos dois lados: {d_mod_frames == r_mod_frames}")
    summary = {
        "link_id": args.link_id, "after": args.after, "strict_compared": n, "first_divergence": div,
        "module_events": len(module_events), "strict_reads": n - len(module_events), "stream_files": n_files,
        "stream_reads": stream_reads, "stream_reads_identical": stream_same, "stream_bad_segments": bad_segments,
        "module_frames_equal": d_mod_frames == r_mod_frames,
        "strict_sha256": sha([e for _, e in d_strict[:n]]) if div is None else None,
        "dolphin": d_info, "runtime": r_info, "ok": ok,
    }
    if args.summary_json:
        Path(args.summary_json).write_text(json.dumps(summary, indent=2) + "\n")
    print("OK" if ok else "FALHOU")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
