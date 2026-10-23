"""Oráculo do M4: roda o jogo no Dolphin com o stub do GDB e grava (a) o estado no entry point e (b) o trace de
entradas de função até a primeira chamada de `main`. Saída em logs/ (fora do git). docs/M4_BOOT.md §5.

    python3 -I scripts/dolphin_trace.py boot-state --iso <rvz> --out logs/m4/dolphin_boot
    python3 -I scripts/dolphin_trace.py trace --iso <rvz> --dol <main.dol> --out logs/m4/dolphin_trace.txt
    python3 -I scripts/dolphin_trace.py events --iso <rvz> --frames 600 --out logs/m5/dolphin_events.txt

M5 (docs/M5_OS_DISCO.md §6): `events` põe breakpoints só em pontos de interesse (main, OSLink/OSLinkFixed/OSUnlink,
a função interna Read do DVD que programa o comando 0xA8 no DI, e a entrada do quadro fn_80051034) e grava uma linha
por evento no mesmo formato que a runtime grava com --events. `trace --after-main N` continua o trace de chamadas
depois de main até N eventos.

Método: o Dolphin (2609a) para no entry point esperando o GDB (Core.cpp: CPUSetInitialExecutionState). Pomos um
breakpoint de execução em cada início de função conhecido (analysis/call_graph.json, símbolos do dtk) e em cada alvo
de bl/bcl direto do DOL; a cada parada lemos LR e CTR. O modo debug do Dolphin precisa estar ligado para o JIT
checar breakpoints (JitArm64/Jit.cpp: IsDebuggingEnabled()).
"""

from __future__ import annotations

import argparse
import atexit
import json
import os
import signal
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
# Endereços do M5 (runtime/include/am/game.h).
OS_LINK = 0x800C5C70
OS_LINK_FIXED = 0x800C5C94
OS_UNLINK = 0x800C5F08
DVD_READ = 0x800CDC70  # Read(addr r3, length r4, offset r5, cb r6): escreve DICMDBUF/DIMAR/DILENGTH e DICR
FRAME = 0x80051034
FRAME_RETURN = 0x8004839C


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
    # O Dolphin fica no grupo de processos do script: with_timeout.sh mata o grupo inteiro mesmo se o Python morrer
    # com SIGKILL. SIGTERM/SIGINT viram SystemExit para os finally rodarem, e o atexit garante o kill em erro
    # inesperado: nenhum Dolphin pode sobreviver ao script (um órfão do M4 ficou 2h30 segurando a porta do GDB).
    proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT)
    atexit.register(kill_group, proc)
    for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(sig, _raise_exit)
    return proc


def _raise_exit(signum, frame) -> None:  # noqa: ARG001
    raise SystemExit(128 + signum)


def kill_group(proc: subprocess.Popen) -> None:
    if proc.poll() is not None:
        return
    for sig, wait in ((signal.SIGTERM, 10), (signal.SIGKILL, 5)):
        try:
            os.kill(proc.pid, sig)
        except ProcessLookupError:
            return
        try:
            proc.wait(timeout=wait)
            return
        except subprocess.TimeoutExpired:
            continue


def stop(proc: subprocess.Popen, client: gdb.GdbClient | None) -> None:
    if client is not None:
        try:
            client.kill()
        except OSError:
            pass
    kill_group(proc)


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


def stop_pc(client: gdb.GdbClient, reply: str) -> int:
    if reply.startswith("T"):
        for field in reply[3:].split(";"):
            if field.startswith("40:"):
                return int(field[3:], 16)
    return client.read_reg(gdb.REG_PC)


def cmd_events(args) -> int:
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    proc = launch(args)
    client = None
    lines: list[str] = []
    frames = 0
    reason = "?"
    t0 = time.monotonic()
    try:
        client = gdb.GdbClient("127.0.0.1", args.port)
        for a in (MAIN, OS_LINK, OS_LINK_FIXED, OS_UNLINK, DVD_READ, FRAME):
            client.add_breakpoint(a)
        while True:
            if time.monotonic() - t0 > args.timeout:
                reason = f"tempo limite de {args.timeout}s"
                break
            pc = stop_pc(client, client.cont())
            r = [client.read_reg(i) for i in (3, 4, 5)]
            lr = client.read_reg(gdb.REG_LR)
            if pc == MAIN:
                lines.append("main")
            elif pc in (OS_LINK, OS_LINK_FIXED, OS_UNLINK):
                mid = int.from_bytes(client.read_mem(r[0], 4), "big")
                kind = "osunlink" if pc == OS_UNLINK else "oslink"
                lines.append(f"{kind} frame={frames} id={mid} real=0x{r[0]:08X}")
            elif pc == DVD_READ:
                lines.append(f"di_read frame={frames} offset=0x{r[2]:09X} length=0x{r[1]:X} dst=0x{r[0] & 0x03FFFFFF:08X}")
            elif pc == FRAME and lr == FRAME_RETURN:
                if frames >= args.frames:
                    reason = "frames"
                    break
                frames += 1
    finally:
        elapsed = time.monotonic() - t0
        stop(proc, client)
        out.write_text(f"# am-events v1 source=dolphin dolphin=2609a frames={frames} end={reason}\n" +
                       "".join(l + "\n" for l in lines))
        print(f"{len(lines)} eventos, {frames} quadros em {elapsed:.1f}s; fim: {reason}; {out}")
    return 0 if reason == "frames" else 1


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
    e = sub.add_parser("events")
    e.add_argument("--out", required=True)
    e.add_argument("--frames", type=int, default=600)
    e.add_argument("--timeout", type=float, default=1800.0)
    args = ap.parse_args()
    if args.cmd == "events":
        return cmd_events(args)
    return cmd_boot_state(args) if args.cmd == "boot-state" else cmd_trace(args)


if __name__ == "__main__":
    sys.exit(main())
