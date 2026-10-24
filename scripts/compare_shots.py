"""Compara duas capturas de XFB (PNG) do M6: a da runtime (--shot) e a do Dolphin (dolphin_trace.py --shots).

    python3 -I scripts/compare_shots.py runtime.png dolphin.png [--side-by-side lado.png] [--diff diff.png]
        [--min-psnr 30] [--max-bad 1.0] [--json resumo.json]

Métricas sobre RGB (docs/M6_PRIMEIRO_QUADRO.md §5): PSNR, erro absoluto médio e máximo por canal, e a fração de
pixels "ruins" (algum canal com diferença > 24, o que já é visível). Critério: PSNR >= --min-psnr e pixels ruins
<= --max-bad (%). Também conta os pixels "acesos" (luminância > 32) de cada lado: uma captura preta contra outra
preta passaria no PSNR sem provar nada, então ambas precisam ter conteúdo (--min-lit, %).

Sem dependências: decodifica PNG de 8 bits RGB/RGBA (com os 5 filtros) e grava PNG sem filtro. Nunca versionar as
imagens (são do jogo); só o resumo vai para tests/golden/.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
import zlib
from pathlib import Path

BAD_DELTA = 24
LIT_LUMA = 32


def read_png(path: Path) -> tuple[int, int, bytes]:
    """Devolve (largura, altura, RGB contíguo)."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: não é PNG")
    pos = 8
    idat = b""
    width = height = channels = 0
    while pos < len(data):
        n = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
            depth, ctype, _, _, interlace = body[8], body[9], body[10], body[11], body[12]
            if depth != 8 or ctype not in (2, 6) or interlace != 0:
                raise SystemExit(f"{path}: só PNG de 8 bits RGB/RGBA sem entrelaçamento (tipo {ctype}, {depth} bits)")
            channels = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    stride = width * channels
    out = bytearray(width * height * 3)
    prev = bytearray(stride)
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if f == 1:
                line[i] = (line[i] + a) & 0xFF
            elif f == 2:
                line[i] = (line[i] + b) & 0xFF
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        for x in range(width):
            out[(y * width + x) * 3:(y * width + x) * 3 + 3] = line[x * channels:x * channels + 3]
        prev = line
    return width, height, bytes(out)


def write_png(path: Path, width: int, height: int, rgb: bytes) -> None:
    rows = b"".join(b"\x00" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return len(payload).to_bytes(4, "big") + kind + payload + zlib.crc32(kind + payload).to_bytes(4, "big")

    ihdr = width.to_bytes(4, "big") + height.to_bytes(4, "big") + bytes([8, 2, 0, 0, 0])
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(rows, 6)) +
                     chunk(b"IEND", b""))


def lit_fraction(rgb: bytes) -> float:
    lit = 0
    for i in range(0, len(rgb), 3):
        if (299 * rgb[i] + 587 * rgb[i + 1] + 114 * rgb[i + 2]) // 1000 > LIT_LUMA:
            lit += 1
    return 100.0 * lit / (len(rgb) // 3)


def compare(a: bytes, b: bytes) -> dict:
    sq = 0
    abs_sum = 0
    max_d = 0
    bad = 0
    for i in range(0, len(a), 3):
        worst = 0
        for k in range(3):
            d = a[i + k] - b[i + k]
            sq += d * d
            ad = abs(d)
            abs_sum += ad
            worst = max(worst, ad)
        max_d = max(max_d, worst)
        if worst > BAD_DELTA:
            bad += 1
    pixels = len(a) // 3
    mse = sq / len(a)
    return {
        "psnr_db": round(99.0 if mse == 0 else 10 * math.log10(255 * 255 / mse), 2),
        "mean_abs": round(abs_sum / len(a), 3),
        "max_abs": max_d,
        "bad_pixels_pct": round(100.0 * bad / pixels, 3),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runtime")
    ap.add_argument("dolphin")
    ap.add_argument("--side-by-side", default=None, help="PNG com runtime à esquerda e Dolphin à direita")
    ap.add_argument("--diff", default=None, help="PNG com |diferença| x4")
    ap.add_argument("--min-psnr", type=float, default=30.0)
    ap.add_argument("--max-bad", type=float, default=1.0, help="%% máximo de pixels com diferença > 24")
    ap.add_argument("--min-lit", type=float, default=1.0, help="%% mínimo de pixels acesos em cada captura")
    ap.add_argument("--json", default=None)
    args = ap.parse_args()
    wa, ha, a = read_png(Path(args.runtime))
    wb, hb, b = read_png(Path(args.dolphin))
    if (wa, ha) != (wb, hb):
        print(f"tamanhos diferentes: runtime {wa}x{ha}, Dolphin {wb}x{hb}")
        return 1
    r = compare(a, b)
    r["lit_runtime_pct"] = round(lit_fraction(a), 2)
    r["lit_dolphin_pct"] = round(lit_fraction(b), 2)
    r["size"] = f"{wa}x{ha}"
    r["runtime_rgb_sha256"] = hashlib.sha256(a).hexdigest()
    ok = (r["psnr_db"] >= args.min_psnr and r["bad_pixels_pct"] <= args.max_bad and
          r["lit_runtime_pct"] >= args.min_lit and r["lit_dolphin_pct"] >= args.min_lit)
    r["ok"] = ok
    print(f"{Path(args.runtime).name} x {Path(args.dolphin).name}: PSNR {r['psnr_db']} dB, erro médio "
          f"{r['mean_abs']}, máximo {r['max_abs']}, pixels com diferença > {BAD_DELTA}: {r['bad_pixels_pct']}%, "
          f"acesos {r['lit_runtime_pct']}% / {r['lit_dolphin_pct']}% -> {'OK' if ok else 'FALHOU'}")
    if args.side_by_side:
        rows = bytearray()
        for y in range(ha):
            rows += a[y * wa * 3:(y + 1) * wa * 3] + b[y * wb * 3:(y + 1) * wb * 3]
        write_png(Path(args.side_by_side), wa * 2, ha, bytes(rows))
    if args.diff:
        write_png(Path(args.diff), wa, ha, bytes(min(255, abs(x - y) * 4) for x, y in zip(a, b)))
    if args.json:
        Path(args.json).write_text(json.dumps(r, indent=1) + "\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
