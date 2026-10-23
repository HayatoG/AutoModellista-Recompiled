"""Análise não destrutiva de uma imagem de GameCube e da árvore extraída (só leitura).

    python3 -I scripts/iso_census.py header <disco.iso>          -> cabeçalho do disco (JSON)
    python3 -I scripts/iso_census.py files  <pasta_extraída>     -> censo de arquivos por formato (JSON)
    python3 -I scripts/iso_census.py libs   <main.dol>           -> strings de SDK/bibliotecas reconhecíveis (JSON)

Nenhum byte do jogo sai da máquina; a saída contém só metadados.
"""

from __future__ import annotations

import collections
import json
import re
import struct
import sys
from pathlib import Path

REGIONS = {"E": "USA (NTSC-U)", "J": "Japão (NTSC-J)", "P": "Europa (PAL)", "D": "Alemanha (PAL)", "F": "França (PAL)"}

# Assinaturas no início do arquivo -> formato. As marcadas "Capcom?" são hipóteses a confirmar.
MAGICS = [
    (b"Yaz0", "Yaz0 (compressão Nintendo)"),
    (b"Yay0", "Yay0 (compressão Nintendo)"),
    (b"RARC", "RARC (arquivo Nintendo)"),
    (b"U\xaa8-", "U8 (arquivo Nintendo)"),
    (b"AFS\x00", "AFS (arquivo CRI)"),
    (b"\x80\x00", "ADX (áudio CRI)?"),
    (b"CPK ", "CPK (arquivo CRI)"),
    (b"\x00\x00\x01\xba", "MPEG-PS / SFD (Sofdec)?"),
    (b"THP\x00", "THP (vídeo Nintendo)"),
    (b"\x00\x20\xaf\x30", "TPL (textura Nintendo)"),
    (b"BNR1", "BNR1 (banner GameCube)"),
    (b"RIFF", "RIFF/WAV"),
    (b"DSP\x00", "DSP ADPCM?"),
]

LIB_PATTERNS = {
    "Dolphin SDK (OS)": rb"<< Dolphin SDK - [A-Z]+\s+release build: [^>]+>>",
    "Nintendo SDK módulos": rb"<< Dolphin SDK - [A-Z]+",
    "Metrowerks / MSL": rb"Metrowerks|MSL_C|__msl_",
    "CodeWarrior runtime": rb"__init_cpp_exceptions|__destroy_global_chain|__register_global_object",
    "MusyX (áudio Factor 5)": rb"MusyX|musyx",
    "CRI Middleware (ADX/Sofdec)": rb"CRI |ADXT|ADXF|CRIWARE|criware|Sofdec|SFD_",
    "JSystem (Nintendo EAD)": rb"JSystem|JKRHeap|J3D",
    "RenderWare": rb"RenderWare|RwEngine|RpWorld",
    "Criterion": rb"Criterion",
    "Capcom": rb"CAPCOM|Capcom",
    "Bink/RAD": rb"Bink|RAD Game Tools",
    "zlib": rb"inflate \d|deflate \d|Jean-loup Gailly",
    "GX": rb"GXInit|__GXSetDirtyState|GX_",
    "DVD": rb"DVDOpen|dvdfs|DVDReadAsync",
    "CARD (memory card)": rb"CARDMount|CARDOpen|__CARD",
    "PAD": rb"PADInit|PADRead",
    "AX/AI/DSP (áudio)": rb"AXInit|AIInit|DSPInit|__AX",
    "VI": rb"VIInit|VIConfigure",
    "BBA / rede (SO-modem)": rb"SOInit|SOSocket|IPInit|ETHInit|bba|modem",
}


def header(iso: Path) -> dict:
    with iso.open("rb") as f:
        h = f.read(0x440)
    game_id = h[0:6].decode("ascii", "replace")
    dol_off, fst_off, fst_size = struct.unpack(">III", h[0x420:0x42C])
    return {
        "game_id": game_id,
        "console": h[0:1].decode("ascii", "replace"),
        "game_code": h[1:3].decode("ascii", "replace"),
        "region_code": h[3:4].decode("ascii", "replace"),
        "region": REGIONS.get(h[3:4].decode("ascii", "replace"), "desconhecida"),
        "maker": h[4:6].decode("ascii", "replace"),
        "disc_number": h[6],
        "revision": h[7],
        "audio_streaming": h[8],
        "magic_ok": h[0x1C:0x20] == b"\xc2\x33\x9f\x3d",
        "title": h[0x20:0x400].split(b"\x00", 1)[0].decode("shift_jis", "replace"),
        "dol_offset": hex(dol_off),
        "fst_offset": hex(fst_off),
        "fst_size": hex(fst_size),
        "boot_bin_size": iso.stat().st_size,
    }


def files(root: Path) -> dict:
    by_ext = collections.Counter()
    by_magic = collections.Counter()
    size_by_ext = collections.Counter()
    rels, listing = [], []
    for p in sorted(root.rglob("*")):
        if not p.is_file():
            continue
        rel = str(p.relative_to(root))
        size = p.stat().st_size
        ext = p.suffix.lower() or "(sem extensão)"
        by_ext[ext] += 1
        size_by_ext[ext] += size
        with p.open("rb") as f:
            head = f.read(16)
        if ext == ".rel":
            fmt = f"REL (módulo id {int.from_bytes(head[0:4], 'big')})"
        else:
            fmt = next((name for magic, name in MAGICS if head.startswith(magic)), "desconhecido")
        by_magic[fmt] += 1
        if ext in (".rel", ".rso", ".sel", ".str"):
            rels.append(rel)
        listing.append({"path": rel, "size": size, "magic": head[:8].hex(), "format": fmt})
    return {
        "total_files": len(listing),
        "total_bytes": sum(x["size"] for x in listing),
        "by_extension": {k: {"count": by_ext[k], "bytes": size_by_ext[k]} for k in sorted(by_ext)},
        "by_magic": dict(by_magic.most_common()),
        "modules": rels,
        "files": listing,
    }


def libs(dol: Path) -> dict:
    data = dol.read_bytes()
    found = {}
    for name, pat in LIB_PATTERNS.items():
        hits = sorted({m.group(0).decode("latin-1") for m in re.finditer(pat, data)})
        if hits:
            found[name] = {"count": len(list(re.finditer(pat, data))), "samples": hits[:12]}
    return found


def main() -> None:
    cmd, target = sys.argv[1], Path(sys.argv[2])
    out = {"header": header, "files": files, "libs": libs}[cmd](target)
    json.dump(out, sys.stdout, indent=2, ensure_ascii=False)
    print()


if __name__ == "__main__":
    main()
