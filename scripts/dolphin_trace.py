"""Oráculo do M4: roda o jogo no Dolphin com o stub do GDB e grava (a) o estado no entry point e (b) o trace de
entradas de função até a primeira chamada de `main`. Saída em logs/ (fora do git). docs/M4_BOOT.md §5.

    python3 -I scripts/dolphin_trace.py boot-state --iso <rvz> --out logs/m4/dolphin_boot
    python3 -I scripts/dolphin_trace.py trace --iso <rvz> --dol <main.dol> --out logs/m4/dolphin_trace.txt

Método: o Dolphin (2609a) para no entry point esperando o GDB (Core.cpp: CPUSetInitialExecutionState). Pomos um
breakpoint de execução em cada início de função conhecido (analysis/call_graph.json, símbolos do dtk) e em cada alvo
de bl/bcl direto do DOL; a cada parada lemos LR e CTR. O modo debug do Dolphin precisa estar ligado para o JIT
checar breakpoints (JitArm64/Jit.cpp: IsDebuggingEnabled()).
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import dolphin_gdb as gdb  # noqa: E402
from am_trace_lib import Dol, direct_call_targets, load_function_names, write_trace  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DOLPHIN = "/Applications/Dolphin.app/Contents/MacOS/Dolphin"
DEFAULT_USER = "/Volumes/AMRecomp/dolphin-user"
ENTRY = 0x80003154
MAIN = 0x800719AC


def launch(args) -> subprocess.Popen:
    cmd = [
        args.dolphin,
        "-u", args.user_dir,
        "-b",
        "-v", "Null",
        "-C", f"Dolphin.General.GDBPort={args.port}",
        "-C", "Dolphin.Interface.DebugModeEnabled=True",
        "-C", "Dolphin.Analytics.PermissionAsked=True",
        "-C", "Dolphin.Analytics.Enabled=False",
        "-C", f"Dolphin.Core.CPUCore={args.cpu_core}",
        "-C", "Dolphin.Core.SkipIPL=True",
        "-C", f"Dolphin.Core.DSPHLE={'True' if args.dsp_hle else 'False'}",
        "-C", "Dolphin.DSP.Backend=No Audio Output",
        "-C", "Dolphin.Core.EnableCheats=False",
        # Dispositivos iguais aos da runtime no M4 (docs/M4_BOOT.md §4): nenhum aparelho EXI (o padrão do Dolphin
        # põe um memory card no slot A; Config/MainSettings.cpp, EXIDeviceType::None = 0xFF) e um controle de
        # GameCube só na porta 1 (SIDEVICE_GC_CONTROLLER = 6, SIDEVICE_NONE = 0; HW/SI/SI_Device.h).
        "-C", "Dolphin.Core.SlotA=255",
        "-C", "Dolphin.Core.SlotB=255",
        "-C", "Dolphin.Core.SerialPort1=255",
        "-C", "Dolphin.Core.SIDevice0=6",
        "-C", "Dolphin.Core.SIDevice1=0",
        "-C", "Dolphin.Core.SIDevice2=0",
        "-C", "Dolphin.Core.SIDevice3=0",
        "-e", args.iso,
    ]
    log = open(args.dolphin_log, "w") if args.dolphin_log else subprocess.DEVNULL
    return subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT)


def stop(proc: subprocess.Popen, client: gdb.GdbClient | None) -> None:
    if client is not None:
        client.kill()
    proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()


def cmd_boot_state(args) -> int:
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    proc = launch(args)
    client = None
    try:
        client = gdb.GdbClient("127.0.0.1", args.port)
        regs = {f"r{i}": client.read_reg(i) for i in range(32)}
        for name, rid in [("pc", gdb.REG_PC), ("msr", gdb.REG_MSR), ("cr", gdb.REG_CR), ("lr", gdb.REG_LR),
                          ("ctr", gdb.REG_CTR), ("xer", gdb.REG_XER), ("fpscr", gdb.REG_FPSCR),
                          ("pvr", gdb.REG_PVR), ("srr0", gdb.REG_SRR0), ("srr1", gdb.REG_SRR1),
                          ("tbl", gdb.REG_TBL), ("tbu", gdb.REG_TBU), ("dec", gdb.REG_DEC),
                          ("hid0", gdb.REG_HID0), ("hid1", gdb.REG_HID1)]:
            regs[name] = client.read_reg(rid)
        for i in range(4):
            regs[f"sprg{i}"] = client.read_reg(gdb.REG_SPRG0 + i)
        if regs["pc"] != ENTRY:
            print(f"aviso: pc=0x{regs['pc']:08X}, esperado 0x{ENTRY:08X}", file=sys.stderr)
        mem = client.read_mem(0x80000000, 24 * 1024 * 1024)
        Path(str(out) + ".mem1.bin").write_bytes(mem)
        meta = {"dolphin": args.dolphin, "cpu_core": args.cpu_core, "dsp_hle": args.dsp_hle,
                "registers": {k: f"0x{v:08X}" for k, v in regs.items()}}
        Path(str(out) + ".json").write_text(json.dumps(meta, indent=1) + "\n")
        print(f"estado no entry point: {out}.json, {out}.mem1.bin")
    finally:
        stop(proc, client)
    return 0


def cmd_trace(args) -> int:
    dol = Dol(Path(args.dol))
    names = load_function_names(Path(args.call_graph))
    starts = set(names) | direct_call_targets(dol) | {MAIN}
    starts = {a for a in starts if dol.read32(a) is not None}
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    Path(str(out) + ".breakpoints").write_text("".join(f"{a:08X}\n" for a in sorted(starts)))

    proc = launch(args)
    client = None
    events: list[tuple[int, int, int]] = []
    reason = "?"
    t0 = time.monotonic()
    try:
        client = gdb.GdbClient("127.0.0.1", args.port)
        pc0 = client.read_reg(gdb.REG_PC)
        if pc0 != ENTRY:
            print(f"aviso: parado em 0x{pc0:08X}, esperado 0x{ENTRY:08X}", file=sys.stderr)
        for a in sorted(starts):
            client.add_breakpoint(a)
        while True:
            if len(events) >= args.max_events:
                reason = f"limite de {args.max_events} eventos"
                break
            if time.monotonic() - t0 > args.timeout:
                reason = f"tempo limite de {args.timeout}s"
                break
            reply = client.cont()
            pc = None
            if reply.startswith("T"):
                for field in reply[3:].split(";"):
                    if field.startswith("40:"):
                        pc = int(field[3:], 16)
            if pc is None:
                pc = client.read_reg(gdb.REG_PC)
            lr = client.read_reg(gdb.REG_LR)
            ctr = client.read_reg(gdb.REG_CTR)
            events.append((pc, lr, ctr))
            if pc == MAIN:
                reason = "main"
                break
    finally:
        elapsed = time.monotonic() - t0
        stop(proc, client)
        header = {
            "source": "dolphin",
            "dolphin": "2609a",
            "cpu_core": str(args.cpu_core),
            "dsp_hle": str(args.dsp_hle),
            "kind": "raw-breakpoints",
            "breakpoints": str(len(starts)),
            "end": reason.replace(" ", "_"),
        }
        write_trace(out, header, events)
        print(f"{len(events)} paradas em {elapsed:.1f}s; fim: {reason}; trace: {out}")
    return 0 if reason == "main" else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dolphin", default=os.environ.get("DOLPHIN", DEFAULT_DOLPHIN))
    ap.add_argument("--user-dir", default=DEFAULT_USER)
    ap.add_argument("--iso", required=True)
    ap.add_argument("--port", type=int, default=2345)
    ap.add_argument("--cpu-core", type=int, default=4, help="0 Interpreter, 1 JIT64, 4 JITARM64, 5 CachedInterpreter")
    ap.add_argument("--dsp-hle", type=lambda s: s.lower() in ("1", "true", "yes"), default=True)
    ap.add_argument("--dolphin-log", default=None)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("boot-state")
    b.add_argument("--out", required=True)
    t = sub.add_parser("trace")
    t.add_argument("--dol", required=True)
    t.add_argument("--call-graph", default=str(ROOT / "analysis" / "call_graph.json"))
    t.add_argument("--out", required=True)
    t.add_argument("--max-events", type=int, default=200000)
    t.add_argument("--timeout", type=float, default=1800.0)
    args = ap.parse_args()
    return cmd_boot_state(args) if args.cmd == "boot-state" else cmd_trace(args)


if __name__ == "__main__":
    sys.exit(main())
