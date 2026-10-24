"""Compara dois WAV do M7: o --audio-dump da runtime e o dspdump.wav do Dolphin (dolphin_trace.py --audio-dump), do
mesmo roteiro e do mesmo --rtc. docs/M7_MENUS.md §5.

    python3 -I scripts/compare_audio.py runtime.wav dolphin.wav [--start 0] [--end 0] [--window 1.0]
        [--json resumo.json] [--min-corr 0.9] [--max-db 1.0] [--min-ok 90]

Os dois lados geram o áudio no ritmo do tempo emulado, mas o jogo não chega aos mesmos pontos no mesmo ciclo (a
runtime estima os ciclos do C gerado; docs/M5_OS_DISCO.md §5.3): o mesmo som pode começar alguns ms antes ou depois.
A métrica tolera isso alinhando antes de comparar:

1. Envelope: RMS do canal mono (L + R) / 2 em blocos de 10 ms, em dBFS.
2. Defasagem global: o deslocamento (em blocos, até ±2 s) que maximiza a correlação dos envelopes; depois refinado
   em amostras (±1 bloco) pela correlação cruzada das amostras no trecho de 1 s mais alto.
3. Por janela (--window s, padrão 1 s), com a defasagem global e um ajuste local de até ±50 ms (--local, buscado
   no sinal com média de 8 amostras e refinado em ±8 amostras): a correlação normalizada das amostras (1,0 = mesma
   forma de onda) e a diferença de nível RMS em dB.
4. Janelas "ativas" (as duas acima de -60 dBFS) contam no critério; janela ativa de um lado e muda do outro é
   divergência de presença.

Critério padrão: nas janelas ativas, pelo menos --min-ok % com correlação >= --min-corr e |diferença de nível| <=
--max-db; e nenhuma janela com presença divergente. Sem dependências (só a biblioteca padrão). Nunca versionar os
WAV (são do jogo); só o resumo vai para tests/golden/.
"""

from __future__ import annotations

import argparse
import array
import json
import math
import operator
import struct
import sys
from pathlib import Path

BLOCK = 320  # amostras por bloco do envelope (10 ms a 32 kHz)
SILENT_DB = -60.0


def read_wav(path: Path) -> tuple[int, array.array]:
    """Devolve (taxa, mono). Usa o tamanho do arquivo, não o do cabeçalho: o Dolphin morto pelo oráculo não corrige
    os tamanhos (WaveFileWriter grava 100 MB provisórios)."""
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit(f"{path}: não é WAV")
    pos = 12
    rate = channels = bits = 0
    while pos + 8 <= len(data):
        kind = data[pos:pos + 4]
        size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
        if kind == b"fmt ":
            _, channels, rate = struct.unpack("<HHI", data[pos + 8:pos + 16])
            bits = struct.unpack("<H", data[pos + 22:pos + 24])[0]
        if kind == b"data":
            body = data[pos + 8:]
            break
        pos += 8 + size
    else:
        raise SystemExit(f"{path}: sem bloco data")
    if channels != 2 or bits != 16:
        raise SystemExit(f"{path}: esperado PCM 16 bits estéreo, achei {channels} canais de {bits} bits")
    body = body[: len(body) // 4 * 4]
    pcm = array.array("h")
    pcm.frombytes(body)
    if sys.byteorder == "big":
        pcm.byteswap()
    mono = array.array("i", map(operator.add, pcm[0::2], pcm[1::2]))  # L + R (escala x2, igual nos dois lados)
    return rate, mono


def envelope(x: array.array) -> list[float]:
    out = []
    for i in range(0, len(x) - BLOCK + 1, BLOCK):
        seg = x[i:i + BLOCK]
        e = sum(map(operator.mul, seg, seg)) / BLOCK
        out.append(10.0 * math.log10(e / (2.0 * 32768.0) ** 2) if e > 0 else -120.0)
    return out


def corr(a, b) -> float:
    n = min(len(a), len(b))
    if n == 0:
        return 0.0
    sab = sum(map(operator.mul, a[:n], b[:n]))
    saa = sum(map(operator.mul, a[:n], a[:n]))
    sbb = sum(map(operator.mul, b[:n], b[:n]))
    return sab / math.sqrt(saa * sbb) if saa > 0 and sbb > 0 else 0.0


def envelope_lag(ea: list[float], eb: list[float], max_lag: int) -> int:
    """Deslocamento d (blocos) que maximiza a correlação de ea[i] com eb[i + d]."""
    best, best_d = -2.0, 0
    for d in range(-max_lag, max_lag + 1):
        a = ea[max(0, -d):]
        b = eb[max(0, d):]
        n = min(len(a), len(b))
        if n < 100:
            continue
        ma = sum(a[:n]) / n
        mb = sum(b[:n]) / n
        c = corr([v - ma for v in a[:n]], [v - mb for v in b[:n]])
        if c > best:
            best, best_d = c, d
    return best_d


def sample_lag(a: array.array, b: array.array, start: int, length: int, center: int, radius: int) -> tuple[int, float]:
    """Melhor deslocamento d em [center - radius, center + radius] para a[start:] contra b[start + d:]."""
    seg = a[start:start + length]
    best, best_d = -2.0, center
    for d in range(center - radius, center + radius + 1):
        s = start + d
        if s < 0 or s + length > len(b):
            continue
        c = corr(seg, b[s:s + length])
        if c > best:
            best, best_d = c, d
    return best_d, best


DECIM = 8


def decimate(x: array.array) -> array.array:
    """Média de DECIM amostras: busca grossa da defasagem por janela, refinada depois em amostras."""
    return array.array("i", (sum(x[i:i + DECIM]) // DECIM for i in range(0, len(x) - DECIM + 1, DECIM)))


def window_lag(a, b, a8, b8, start: int, length: int, center: int, radius: int) -> tuple[int, float]:
    """Defasagem da janela: busca em a8/b8 (1/DECIM) em ±radius, depois ±DECIM em amostras."""
    d8, _ = sample_lag(a8, b8, start // DECIM, length // DECIM, center // DECIM, radius // DECIM + 1)
    return sample_lag(a, b, start, length, d8 * DECIM, DECIM)


def level_db(x) -> float:
    e = sum(map(operator.mul, x, x)) / max(1, len(x))
    return 10.0 * math.log10(e / (2.0 * 32768.0) ** 2) if e > 0 else -120.0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runtime")
    ap.add_argument("dolphin")
    ap.add_argument("--start", type=float, default=0.0, help="início do trecho comparado (s, no tempo da runtime)")
    ap.add_argument("--end", type=float, default=0.0, help="fim do trecho (s); 0 = até o fim do mais curto")
    ap.add_argument("--window", type=float, default=1.0)
    ap.add_argument("--min-corr", type=float, default=0.9)
    ap.add_argument("--max-db", type=float, default=1.0)
    ap.add_argument("--min-ok", type=float, default=90.0, help="porcentagem mínima de janelas ativas dentro do critério")
    ap.add_argument("--local", type=int, default=1600, help="ajuste local por janela, em amostras (1600 = 50 ms)")
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    rate_a, a = read_wav(Path(args.runtime))
    rate_b, b = read_wav(Path(args.dolphin))
    if rate_a != rate_b:
        raise SystemExit(f"taxas diferentes: {rate_a} x {rate_b}")
    ea, eb = envelope(a), envelope(b)
    lag_blocks = envelope_lag(ea, eb, int(2.0 * rate_a / BLOCK))
    # Refinamento em amostras no segundo mais alto da runtime.
    win = int(args.window * rate_a)
    loud = max(range(0, max(1, len(ea) - win // BLOCK)), key=lambda i: sum(ea[i:i + win // BLOCK]))
    lag, _ = sample_lag(a, b, loud * BLOCK, min(win, len(a) - loud * BLOCK), lag_blocks * BLOCK, BLOCK)

    a8, b8 = decimate(a), decimate(b)
    start = int(args.start * rate_a)
    end = int(args.end * rate_a) if args.end > 0 else len(a)
    end = min(end, len(a), len(b) - lag)
    windows = []
    for s in range(max(start, -lag), end - win + 1, win):
        wa = a[s:s + win]
        la = level_db(wa)
        lb = level_db(b[s + lag:s + lag + win])
        active_a, active_b = la > SILENT_DB, lb > SILENT_DB
        entry = {"t": round(s / rate_a, 2), "db_runtime": round(la, 2), "db_dolphin": round(lb, 2)}
        if active_a and active_b:
            d, c = window_lag(a, b, a8, b8, s, win, lag, args.local)
            lb2 = level_db(b[s + d:s + d + win])
            entry.update({"corr": round(c, 4), "lag": d, "db_diff": round(la - lb2, 2)})
        entry["presence"] = "ambos" if active_a and active_b else ("nenhum" if not (active_a or active_b) else "um")
        windows.append(entry)

    active = [w for w in windows if w["presence"] == "ambos"]
    ok = [w for w in active if w["corr"] >= args.min_corr and abs(w["db_diff"]) <= args.max_db]
    diverge = [w for w in windows if w["presence"] == "um"]
    corrs = sorted(w["corr"] for w in active)
    diffs = sorted(abs(w["db_diff"]) for w in active)
    pct_ok = 100.0 * len(ok) / len(active) if active else 0.0
    passed = bool(active) and pct_ok >= args.min_ok and not diverge
    summary = {
        "rate": rate_a,
        "seconds_runtime": round(len(a) / rate_a, 2),
        "seconds_dolphin": round(len(b) / rate_b, 2),
        "lag_samples": lag,
        "lag_ms": round(1000.0 * lag / rate_a, 2),
        "windows": len(windows),
        "active": len(active),
        "silent_both": sum(1 for w in windows if w["presence"] == "nenhum"),
        "presence_diverge": len(diverge),
        "ok": len(ok),
        "pct_ok": round(pct_ok, 1),
        "corr_median": round(corrs[len(corrs) // 2], 4) if corrs else 0.0,
        "corr_min": round(corrs[0], 4) if corrs else 0.0,
        "db_diff_median": round(diffs[len(diffs) // 2], 2) if diffs else 0.0,
        "db_diff_max": round(diffs[-1], 2) if diffs else 0.0,
        "criteria": {"min_corr": args.min_corr, "max_db": args.max_db, "min_ok": args.min_ok},
        "passed": passed,
    }
    for w in windows:
        if w["presence"] != "nenhum" and (w["presence"] == "um" or w not in ok):
            print(f"  janela {w['t']:7.2f}s: runtime {w['db_runtime']:6.1f} dB, Dolphin {w['db_dolphin']:6.1f} dB"
                  + (f", corr {w['corr']:.3f}, dif {w['db_diff']:+.2f} dB, lag {w['lag']}" if "corr" in w else
                     " (presença divergente)"))
    print(f"{Path(args.runtime).name} x {Path(args.dolphin).name}: {summary['seconds_runtime']} s x "
          f"{summary['seconds_dolphin']} s, defasagem {summary['lag_ms']} ms ({lag} amostras); {len(active)} janelas "
          f"ativas de {len(windows)}, {summary['silent_both']} mudas nos dois, {len(diverge)} divergentes; "
          f"correlação mediana {summary['corr_median']} (mín. {summary['corr_min']}), |dif. de nível| mediana "
          f"{summary['db_diff_median']} dB (máx. {summary['db_diff_max']}); {summary['pct_ok']}% dentro "
          f"-> {'OK' if passed else 'FALHOU'}")
    if args.json:
        Path(args.json).write_text(json.dumps({"summary": summary, "windows": windows}, indent=1) + "\n")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
