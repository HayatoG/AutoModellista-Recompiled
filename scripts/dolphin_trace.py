"""Oráculo do M4: roda o jogo no Dolphin com o stub do GDB e grava (a) o estado no entry point e (b) o trace de
entradas de função até a primeira chamada de `main`. Saída em logs/ (fora do git). docs/M4_BOOT.md §5.

    python3 -I scripts/dolphin_trace.py boot-state --iso <rvz> --out logs/m4/dolphin_boot
    python3 -I scripts/dolphin_trace.py trace --iso <rvz> --dol <main.dol> --out logs/m4/dolphin_trace.txt
    python3 -I scripts/dolphin_trace.py events --iso <rvz> --frames 600 --out logs/m5/dolphin_events.txt

M5 (docs/M5_OS_DISCO.md §6): `events` põe breakpoints só em pontos de interesse (main, OSLink/OSLinkFixed/OSUnlink,
a função interna Read do DVD que programa o comando 0xA8 no DI, e a entrada do quadro fn_80051034) e grava uma linha
por evento no mesmo formato que a runtime grava com --events. `trace --after-main N` continua o trace de chamadas
depois de main até N eventos.

Entrada (`events --pad-script`): o mesmo roteiro da runtime ("quadro:BOTÃO[+BOTÃO]:duração,...", am/pad_script.h)
vira linhas PRESS/RELEASE no controle por pipe do Dolphin (Pipes/pad1 no diretório de usuário, ligado à porta 1 em
Config/GCPadNew.ini). O roteiro é aplicado quando o quadro N começa (breakpoint em fn_80051034), como na runtime; o
Dolphin lê o pipe na próxima leitura do controle, então o quadro exato pode diferir em 1: compare a sequência de
eventos, não o número do quadro. `--shots 300,800` grava o XFB (YUV 4:2:2, endereço dado ao VISetNextFrameBuffer)
desses quadros em PNG, para ver em que tela o jogo está.

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
DEFAULT_USER_PIPE = "/Volumes/AMRecomp/dolphin-user-pipe"  # com --pad-script: GCPadNew.ini aponta para o pipe
ENTRY = 0x80003154
RTC_EPOCH_2000 = 946684800  # 2000-01-01 em segundos Unix (Config/MainSettings.cpp: MAIN_CUSTOM_RTC_VALUE)
MAIN = 0x800719AC
# Endereços do M5 (runtime/include/am/game.h).
OS_LINK = 0x800C5C70
OS_LINK_FIXED = 0x800C5C94
OS_UNLINK = 0x800C5F08
DVD_READ = 0x800CDC70  # Read(addr r3, length r4, offset r5, cb r6): escreve DICMDBUF/DIMAR/DILENGTH e DICR
FRAME = 0x80051034
DVD_IRQ = 0x800CD89C  # __DVDInterruptHandler (só com --timebase-out)
FRAME_RETURN = 0x8004839C
# VISetNextFrameBuffer (0x800D358C) guarda o ponteiro do próximo XFB nesta variável do vi.c, que o VIFlush
# (0x800D345C) copia para o registro do VI (REA/Ghidra; analysis/known_addresses.md).
VI_NEXT_FB_VAR = 0x8026E648
XFB_W, XFB_H = 640, 448
# Máscaras e nomes do runtime/core/pad_script.c (nomes do controle por pipe do Dolphin).
PAD_BUTTONS = {"D_LEFT": 0x0001, "D_RIGHT": 0x0002, "D_DOWN": 0x0004, "D_UP": 0x0008, "Z": 0x0010, "A": 0x0100,
               "B": 0x0200, "X": 0x0400, "Y": 0x0800, "START": 0x1000}
PAD_GCPAD_INI = """[GCPad1]
Device = Pipe/0/pad1
Buttons/A = `Button A`
Buttons/B = `Button B`
Buttons/X = `Button X`
Buttons/Y = `Button Y`
Buttons/Z = `Button Z`
Buttons/Start = `Button START`
D-Pad/Up = `Button D_UP`
D-Pad/Down = `Button D_DOWN`
D-Pad/Left = `Button D_LEFT`
D-Pad/Right = `Button D_RIGHT`
"""


def launch(args) -> subprocess.Popen:
    cmd = [
        args.dolphin,
        "-u", args.user_dir,
        "-b",
        "-v", args.video,
        "-C", f"Dolphin.General.GDBPort={args.port}",
        "-C", "Dolphin.Interface.DebugModeEnabled=True",
        "-C", "Dolphin.Analytics.PermissionAsked=True",
        "-C", "Dolphin.Analytics.Enabled=False",
        "-C", f"Dolphin.Core.CPUCore={args.cpu_core}",
        "-C", "Dolphin.Core.SkipIPL=True",
        "-C", f"Dolphin.Core.DSPHLE={'True' if args.dsp_hle else 'False'}",
        "-C", "Dolphin.DSP.Backend=No Audio Output",
        "-C", "Dolphin.Core.EnableCheats=False",
        # Relógio igual ao da runtime (--rtc, segundos desde 2000-01-01): o padrão do Dolphin é o relógio do host, e o
        # jogo usa o tempo para sortear (M5: os carros da corrida mudavam de uma execução para outra).
        "-C", "Dolphin.Core.EnableCustomRTC=True",
        "-C", f"Dolphin.Core.CustomRTCValue={RTC_EPOCH_2000 + args.rtc}",
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
        # Sem foco de janela (modo batch) o Dolphin ignora a entrada, a menos que a entrada em segundo plano valha.
        "-C", "Dolphin.Input.BackgroundInput=True",
        "-e", args.iso,
    ]
    for extra in args.dolphin_config:
        cmd[-2:-2] = ["-C", extra]
    if getattr(args, "memcard", None):
        # M8 parte 2b: memory card no slot A (EXIDeviceType::MemoryCard = 1), imagem .raw que já existe. O Dolphin
        # reescreve o nome para <nome>.<região>[.<blocos>].raw (Config/MainSettings.cpp: GetMemcardPath): passe um
        # caminho que já termine em ".USA.raw" para o arquivo ser o mesmo. Vem depois do SlotA=255 acima, e o último
        # -C vale.
        cmd[-2:-2] = ["-C", "Dolphin.Core.SlotA=1", "-C", f"Dolphin.Core.MemcardAPath={args.memcard}"]
    if getattr(args, "audio_dump", None):
        # M7: o Dolphin grava a saída da DMA de áudio (dspdump.wav) e a do streaming do disco (dtkdump.wav) em
        # <user>/Dump/Audio; DumpAudioSilent evita a pergunta de apagar arquivo existente. Antes do -e, como os outros.
        cmd[-2:-2] = ["-C", "Dolphin.DSP.DumpAudio=True", "-C", "Dolphin.DSP.DumpAudioSilent=True"]
        for old in (Path(args.user_dir) / "Dump" / "Audio").glob("*dump*.wav"):
            old.unlink()
    if args.video != "Null":
        # O XFB só existe na RAM se as cópias EFB->XFB forem gravadas nela (o padrão é só textura na GPU; o backend
        # Null nem copia). Necessário para --shots. "-C GFX.Hacks..." não teve efeito no 2609a; o GFX.ini do
        # diretório de usuário tem.
        config = Path(args.user_dir) / "Config"
        config.mkdir(parents=True, exist_ok=True)
        (config / "GFX.ini").write_text("[Hacks]\nXFBToTextureEnable = False\nDeferEFBCopies = False\n")
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
    exact_stops: dict[int, tuple[str, int]] = {}
    saved_r0, probe_regs = 0, (0, 0, 0, 0)
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

    exact_stops: dict[int, tuple[str, int]] = {}
    saved_r0, probe_regs = 0, (0, 0, 0, 0)
    proc = launch(args)
    client = None
    events: list[tuple[int, int, int]] = []
    reason = "?"
    after_main = -1  # eventos depois de main (M5: --after-main)
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
            if after_main >= 0:
                after_main += 1
                if after_main >= args.after_main:
                    reason = "after_main"
                    break
            if pc == MAIN:
                if args.after_main <= 0:
                    reason = "main"
                    break
                after_main = 0
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
    return 0 if reason in ("main", "after_main") else 1


def stop_pc(client: gdb.GdbClient, reply: str) -> int:
    if reply.startswith("T"):
        for field in reply[3:].split(";"):
            if field.startswith("40:"):
                return int(field[3:], 16)
    return client.read_reg(gdb.REG_PC)


def parse_pad_script(text: str) -> list[tuple[int, int, int]]:
    """Mesma gramática de runtime/core/pad_script.c: (quadro, duração, máscara)."""
    steps = []
    for item in filter(None, text.split(",")):
        frame, names, hold = item.split(":")
        mask = 0
        for name in names.split("+"):
            if name not in PAD_BUTTONS:
                raise SystemExit(f"botão desconhecido no roteiro: {name}")
            mask |= PAD_BUTTONS[name]
        if int(hold) <= 0:
            raise SystemExit(f"duração inválida no roteiro: {item}")
        steps.append((int(frame), int(hold), mask))
    return steps


def pad_buttons(steps: list[tuple[int, int, int]], frame: int) -> int:
    mask = 0
    for start, hold, m in steps:
        if start <= frame < start + hold:
            mask |= m
    return mask


class PadPipe:
    """Escreve no pipe nomeado que o Dolphin lê como controle (InputCommon/ControllerInterface/Pipes)."""

    def __init__(self, user_dir: str) -> None:
        pipes = Path(user_dir) / "Pipes"
        pipes.mkdir(parents=True, exist_ok=True)
        self.path = pipes / "pad1"
        if self.path.exists() and not self.path.is_fifo():
            self.path.unlink()
        if not self.path.exists():
            os.mkfifo(self.path)
        config = Path(user_dir) / "Config"
        config.mkdir(parents=True, exist_ok=True)
        (config / "GCPadNew.ini").write_text(PAD_GCPAD_INI)
        self.fd = -1
        self.state = 0

    def open(self, timeout: float = 60.0) -> None:
        """O Dolphin abre o pipe para leitura ao iniciar a entrada; antes disso abrir para escrita dá ENXIO."""
        t0 = time.monotonic()
        while self.fd < 0:
            try:
                self.fd = os.open(self.path, os.O_WRONLY | os.O_NONBLOCK)
            except OSError:
                if time.monotonic() - t0 > timeout:
                    raise
                time.sleep(0.05)

    def set(self, mask: int) -> None:
        lines = []
        for name, bit in PAD_BUTTONS.items():
            if (mask ^ self.state) & bit:
                lines.append(f"{'PRESS' if mask & bit else 'RELEASE'} {name}\n")
        if lines:
            os.write(self.fd, "".join(lines).encode())
        self.state = mask

    def close(self) -> None:
        if self.fd >= 0:
            os.close(self.fd)
            self.fd = -1


def write_xfb_png(path: Path, data: bytes, width: int, height: int) -> None:
    """XFB do GameCube: YUYV 4:2:2 (Y0 U Y1 V), BT.601 de faixa limitada."""
    import zlib

    rows = []
    for y in range(height):
        row = bytearray([0])
        base = y * width * 2
        for x in range(0, width, 2):
            y0, u, y1, v = data[base + x * 2: base + x * 2 + 4]
            for yy in (y0, y1):
                c = 1.164 * (yy - 16)
                r = c + 1.596 * (v - 128)
                g = c - 0.392 * (u - 128) - 0.813 * (v - 128)
                b = c + 2.017 * (u - 128)
                row += bytes(max(0, min(255, int(k))) for k in (r, g, b))
        rows.append(bytes(row))

    def chunk(kind: bytes, payload: bytes) -> bytes:
        return (len(payload).to_bytes(4, "big") + kind + payload +
                zlib.crc32(kind + payload).to_bytes(4, "big"))

    ihdr = width.to_bytes(4, "big") + height.to_bytes(4, "big") + bytes([8, 2, 0, 0, 0])
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) +
                     chunk(b"IEND", b""))


def collect_audio_dump(args) -> None:
    """Move o dspdump.wav desta execução para --audio-dump. O Dolphin é morto antes de fechar o WAV: o cabeçalho fica
    com os tamanhos provisórios (100 MB) e quem lê usa o tamanho do arquivo (scripts/compare_audio.py)."""
    dumps = sorted((Path(args.user_dir) / "Dump" / "Audio").glob("*_dspdump.wav"), key=lambda q: q.stat().st_mtime)
    if not dumps:
        print("aviso: o Dolphin não gravou dspdump.wav", file=sys.stderr)
        return
    target = Path(args.audio_dump)
    target.parent.mkdir(parents=True, exist_ok=True)
    dumps[-1].replace(target)
    for extra in (Path(args.user_dir) / "Dump" / "Audio").glob("*dump*.wav"):
        extra.unlink()
    print(f"áudio do Dolphin: {target} ({target.stat().st_size} bytes)")


# M8 parte 2b: o TBL do GDB é o do último mftb. Com --probes-exact, cada sonda (qualquer instrução que não seja desvio)
# vira um desvio para um trecho posto sobre o OSDumpContext (0x800C43E8, 0x2A8 bytes; só roda num erro fatal):
#     A: mftb r0 ; B: <a instrução original> ; b <sonda + 4>
# com breakpoints em A e em B. Em A lê-se r0; ao continuar parado num breakpoint o Dolphin executa a instrução no
# interpretador (CPU.cpp), cujo mftb lê o tempo exato; em B lê-se o tempo em r0 e r0 volta ao valor de A. Custa 2
# instruções emuladas por passagem (o desvio de ida e o de volta) e o mftb, e muda o "último mftb" do jogo.
EXACT_TB_STUBS = 0x800C43E8
EXACT_TB_MAX = 0x2A8 // 12
EXACT_TB_MFTB_R0 = 0x7C0C42E6


def branch(src: int, dst: int) -> int:
    return 0x48000000 | ((dst - src) & 0x03FFFFFC)


def install_exact_probes(client: gdb.GdbClient, probes: list[int]) -> dict[int, tuple[str, int]]:
    """Instala os trechos; devolve {breakpoint: ("A" ou "B", endereço da sonda)}."""
    if len(probes) > EXACT_TB_MAX:
        raise SystemExit(f"--probes-exact: no máximo {EXACT_TB_MAX} sondas")
    stops = {}
    for i, a in enumerate(probes):
        stub = EXACT_TB_STUBS + 12 * i
        first = client.read_mem(a, 4)
        if first[0] >> 2 in (16, 18, 19):  # bc, b, bclr/bcctr: não podem mudar de lugar
            raise SystemExit(f"--probes-exact: {a:08X} é um desvio")
        code = EXACT_TB_MFTB_R0.to_bytes(4, "big") + first + branch(stub + 8, a + 4).to_bytes(4, "big")
        client.write_mem(stub, code)
        client.write_mem(a, branch(a, stub).to_bytes(4, "big"))
        client.add_breakpoint(stub)
        client.add_breakpoint(stub + 4)
        stops[stub] = ("A", a)
        stops[stub + 4] = ("B", a)
    return stops


def cmd_events(args) -> int:
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    steps = parse_pad_script(args.pad_script) if args.pad_script else []
    shots = {int(f) for f in args.shots.split(",") if f} if args.shots else set()
    if steps and args.user_dir == DEFAULT_USER:
        args.user_dir = DEFAULT_USER_PIPE
    pad = PadPipe(args.user_dir) if steps else None
    # M7: timebase na entrada de cada quadro ("quadro tb"), para comparar o tempo emulado com o da runtime.
    tb_out = open(args.timebase_out, "w") if args.timebase_out else None
    # M8: --probes q0:a,b,... = a partir do quadro q0, "probe pc lr r3 tb" a cada entrada nesses endereços (o mesmo da
    # runtime, runtime/game/game_diag.c). Os breakpoints entram só no quadro q0, para não pagar a parada antes.
    probe_from, probes = 0, []
    if args.probes:
        q0, _, lst = args.probes.partition(":")
        probe_from, probes = int(q0), [int(a, 16) for a in lst.split(",") if a]
        if tb_out is None:
            raise SystemExit("--probes exige --timebase-out")
    exact_stops: dict[int, tuple[str, int]] = {}
    saved_r0, probe_regs = 0, (0, 0, 0, 0)
    proc = launch(args)
    client = None
    lines: list[str] = []
    frames = 0
    reason = "?"
    t0 = time.monotonic()
    try:
        client = gdb.GdbClient("127.0.0.1", args.port)
        if pad is not None:
            pad.open()
        for a in (MAIN, OS_LINK, OS_LINK_FIXED, OS_UNLINK, DVD_READ, FRAME):
            client.add_breakpoint(a)
        if tb_out is not None:
            client.add_breakpoint(DVD_IRQ)
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
                if tb_out is not None:
                    tb = (client.read_reg(gdb.REG_TBU) << 32) | client.read_reg(gdb.REG_TBL)
                    tb_out.write(f"read 0x{r[2]:09X} 0x{r[1]:X} {tb}\n")
            elif pc == DVD_IRQ:
                tb = (client.read_reg(gdb.REG_TBU) << 32) | client.read_reg(gdb.REG_TBL)
                tb_out.write(f"dvdirq {tb}\n")
            elif pc in exact_stops:
                kind, a = exact_stops[pc]
                if kind == "A":
                    saved_r0 = client.read_reg(0)
                    probe_regs = (lr, r[0], r[1], r[2])
                else:
                    tb = (client.read_reg(gdb.REG_TBU) << 32) | client.read_reg(0)
                    client.write_reg(0, saved_r0)
                    tb_out.write(f"probe {a:08X} {probe_regs[0]:08X} {probe_regs[1]:08X} {tb} {tb} "
                                 f"{probe_regs[2]:08X} {probe_regs[3]:08X}\n")
            elif pc in probes and frames >= probe_from:
                tb = (client.read_reg(gdb.REG_TBU) << 32) | client.read_reg(gdb.REG_TBL)
                tb_out.write(f"probe {pc:08X} {lr:08X} {r[0]:08X} {tb}\n")
            elif pc == FRAME and lr == FRAME_RETURN:
                if frames >= args.frames:
                    reason = "frames"
                    break
                frames += 1
                if probes and frames == probe_from:
                    if args.probes_exact:
                        exact_stops = install_exact_probes(client, probes)
                    for a in probes if not args.probes_exact else []:
                        if a not in (MAIN, OS_LINK, OS_LINK_FIXED, OS_UNLINK, DVD_READ, FRAME, DVD_IRQ):
                            client.add_breakpoint(a)
                if tb_out is not None:
                    tb = (client.read_reg(gdb.REG_TBU) << 32) | client.read_reg(gdb.REG_TBL)
                    tb_out.write(f"{frames} {tb}\n")
                if pad is not None:
                    pad.set(pad_buttons(steps, frames))
                xfb = int.from_bytes(client.read_mem(VI_NEXT_FB_VAR, 4), "big") if frames in shots else 0
                if xfb:
                    shot = Path(f"{out.with_suffix('')}_f{frames}.png")
                    data = client.read_mem(xfb, XFB_W * XFB_H * 2)
                    write_xfb_png(shot, data, XFB_W, XFB_H)
                    print(f"quadro {frames}: XFB 0x{xfb:08X} -> {shot}")
    finally:
        elapsed = time.monotonic() - t0
        if pad is not None:
            pad.close()
        if tb_out is not None:
            tb_out.close()
        stop(proc, client)
        if args.audio_dump:
            collect_audio_dump(args)
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
    ap.add_argument("--rtc", type=int, default=0, help="relógio inicial em segundos desde 2000-01-01 (= --rtc da runtime)")
    ap.add_argument("--dolphin-config", action="append", default=[],
                    help="M7: opção extra do Dolphin, 'Sistema.Seção.Chave=valor' (repetível)")
    ap.add_argument("--memcard", default=None, help="M8: arquivo .raw do memory card no slot A (padrão: sem cartão)")
    ap.add_argument("--video", default="Null", help="backend de vídeo do Dolphin (Null; Metal para --shots, que precisa do XFB na RAM)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("boot-state")
    b.add_argument("--out", required=True)
    t = sub.add_parser("trace")
    t.add_argument("--dol", required=True)
    t.add_argument("--call-graph", default=str(ROOT / "analysis" / "call_graph.json"))
    t.add_argument("--out", required=True)
    t.add_argument("--max-events", type=int, default=200000)
    t.add_argument("--after-main", type=int, default=0, help="M5: continua N paradas depois de main")
    t.add_argument("--timeout", type=float, default=1800.0)
    e = sub.add_parser("events")
    e.add_argument("--out", required=True)
    e.add_argument("--frames", type=int, default=600)
    e.add_argument("--timeout", type=float, default=1800.0)
    e.add_argument("--pad-script", default=None, help="roteiro de botões (formato de am/pad_script.h)")
    e.add_argument("--shots", default=None, help="quadros cujo XFB vira PNG ao lado de --out (ex.: 300,800)")
    e.add_argument("--audio-dump", default=None, help="M7: grava o dspdump.wav do Dolphin neste arquivo")
    e.add_argument("--timebase-out", default=None, help="M7: grava 'quadro timebase' na entrada de cada quadro")
    e.add_argument("--probes", default=None, help="M8: 'q0:hex,hex' grava 'probe pc lr r3 tb' a partir do quadro q0")
    e.add_argument("--probes-exact", action="store_true",
                   help="M8 2b: sondas com o timebase exato (por um mftb; aceita qualquer instrução que não seja desvio)")
    args = ap.parse_args()
    if args.cmd == "events":
        return cmd_events(args)
    return cmd_boot_state(args) if args.cmd == "boot-state" else cmd_trace(args)


if __name__ == "__main__":
    sys.exit(main())
