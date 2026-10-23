"""Biblioteca comum dos traces de chamadas do M4 (runtime x Dolphin). Só biblioteca padrão.

Formato do trace (texto, uma linha por evento; docs/M4_BOOT.md §5):

    # am-trace v1 <chave>=<valor> ...      cabeçalho (linhas com '#' são comentário)
    <pc> <lr> <ctr>                         entrada de função: endereço, LR e CTR no momento da entrada (hex, 8 dígitos)

Um evento é uma CHAMADA quando a instrução em LR-4 é um desvio com link que leva a `pc` (ver is_call). A runtime
já grava só chamadas; o Dolphin grava toda parada em breakpoint e o filtro é aplicado aqui, com o mesmo critério.
"""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Section:
    kind: str  # "text" ou "data"
    index: int
    offset: int
    addr: int
    size: int


class Dol:
    """Cabeçalho e conteúdo de um DOL (YAGCD 14.2: 7 seções de texto, 11 de dados, bss e entry point)."""

    def __init__(self, path: Path) -> None:
        self.data = path.read_bytes()
        hdr = struct.unpack(">18I18I18I3I", self.data[:0xE4])
        offs, addrs, sizes = hdr[0:18], hdr[18:36], hdr[36:54]
        self.bss_addr, self.bss_size, self.entry = hdr[54], hdr[55], hdr[56]
        self.sections = [
            Section("text" if i < 7 else "data", i if i < 7 else i - 7, offs[i], addrs[i], sizes[i])
            for i in range(18)
            if sizes[i]
        ]

    def read32(self, addr: int) -> int | None:
        for s in self.sections:
            if s.addr <= addr and addr + 4 <= s.addr + s.size:
                off = s.offset + (addr - s.addr)
                return struct.unpack(">I", self.data[off : off + 4])[0]
        return None

    def text_sections(self) -> list[Section]:
        return [s for s in self.sections if s.kind == "text"]


def branch_with_link_target(word: int, cia: int) -> tuple[str, int | None] | None:
    """Classifica `word` (em `cia`). Devolve (tipo, alvo) se for desvio com link; alvo None = depende de registrador.

    Codificações do PowerPC 750CL User's Manual (cap. 12): b/bl (opcode 18), bc/bcl (16), bclr/bcctr (19, XO 16/528).
    """
    opcode = word >> 26
    if (word & 1) == 0:
        return None
    if opcode == 18:
        li = word & 0x03FFFFFC
        if li & 0x02000000:
            li -= 0x04000000
        target = li if (word & 2) else cia + li
        return ("bl", target & 0xFFFFFFFF)
    if opcode == 16:
        bd = word & 0xFFFC
        if bd & 0x8000:
            bd -= 0x10000
        target = bd if (word & 2) else cia + bd
        return ("bcl", target & 0xFFFFFFFF)
    if opcode == 19:
        xo = (word >> 1) & 0x3FF
        if xo == 528:
            return ("bcctrl", None)
        if xo == 16:
            return ("bclrl", None)
    return None


def is_call(read32, pc: int, lr: int, ctr: int) -> bool:
    """Critério único de "chamada" para os dois lados: a instrução em LR-4 é um desvio com link para `pc`.

    bcctrl confere o alvo pelo CTR; bclrl não pode ser conferido (o LR já foi sobrescrito) e é aceito, exceto quando
    pc == lr (é o blr de volta de quem foi chamado por bclrl).
    """
    if lr < 4:
        return False
    word = read32(lr - 4)
    if word is None:
        return False
    kind = branch_with_link_target(word, lr - 4)
    if kind is None:
        return False
    name, target = kind
    if name == "bcctrl":
        return (ctr & ~3) == pc
    if name == "bclrl":
        return pc != lr
    return target == pc


def direct_call_targets(dol: Dol) -> set[int]:
    """Alvos de todo bl/bcl direto nas seções de texto do DOL."""
    out: set[int] = set()
    for s in dol.text_sections():
        for i in range(0, s.size, 4):
            cia = s.addr + i
            word = struct.unpack(">I", dol.data[s.offset + i : s.offset + i + 4])[0]
            kind = branch_with_link_target(word, cia)
            if kind and kind[1] is not None:
                out.add(kind[1])
    return out


def load_function_names(call_graph: Path) -> dict[int, str]:
    """Endereço -> nome a partir de analysis/call_graph.json (símbolos do dtk)."""
    nodes = json.loads(call_graph.read_text())
    return {int(v["addr"], 16): k for k, v in nodes.items()}


@dataclass
class Trace:
    header: dict[str, str]
    events: list[tuple[int, int, int]]


def read_trace(path: Path) -> Trace:
    header: dict[str, str] = {}
    events: list[tuple[int, int, int]] = []
    for line in path.read_text().splitlines():
        if line.startswith("#"):
            if line.startswith("# am-trace"):
                for item in line.split()[3:]:
                    if "=" in item:
                        k, v = item.split("=", 1)
                        header[k] = v
            continue
        parts = line.split()
        if len(parts) >= 3:
            events.append((int(parts[0], 16), int(parts[1], 16), int(parts[2], 16)))
    return Trace(header, events)


def write_trace(path: Path, header: dict[str, str], events) -> None:
    with path.open("w") as f:
        f.write("# am-trace v1 " + " ".join(f"{k}={v}" for k, v in header.items()) + "\n")
        for pc, lr, ctr in events:
            f.write(f"{pc:08X} {lr:08X} {ctr:08X}\n")


def load_chunks(path: Path) -> list[tuple[int, int]]:
    """Tabela de trechos do C gerado gravada pela runtime (<trace>.chunks): pares (início, fim) ordenados."""
    out = []
    for line in path.read_text().splitlines():
        a, b = line.split()
        out.append((int(a, 16), int(b, 16)))
    return sorted(out)


def chunk_of(chunks: list[tuple[int, int]], addr: int) -> int:
    """Índice do trecho que contém addr, ou -1."""
    import bisect

    i = bisect.bisect_right([c[0] for c in chunks], addr) - 1
    return i if i >= 0 and addr < chunks[i][1] else -1
