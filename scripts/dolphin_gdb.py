"""Cliente mínimo do protocolo remoto do GDB (RSP) para o stub do Dolphin.

Só o que o stub do Dolphin implementa (Source/Core/Core/PowerPC/GDBStub.cpp, tag 2609a):
  p<id>    lê um registrador (ids abaixo)        m<addr>,<len>  lê memória (len <= 4096)
  Z0/z0    põe/tira breakpoint de execução        c              continua até o próximo stop
  k        encerra a sessão
O stub confirma cada pacote com '+' e espera '+' de volta.

Usado por scripts/dolphin_trace.py; não tem dependências fora da biblioteca padrão.
"""

from __future__ import annotations

import socket
import time

# Ids de registrador do ReadRegister() do GDBStub.cpp do Dolphin.
REG_PC = 0x40
REG_MSR = 0x41
REG_CR = 0x42
REG_LR = 0x43
REG_CTR = 0x44
REG_XER = 0x45
REG_FPSCR = 0x46
REG_PVR = 0x57
REG_SPRG0 = 0x6C
REG_SRR0 = 0x70
REG_SRR1 = 0x71
REG_TBL = 0x72
REG_TBU = 0x73
REG_DEC = 0x74
REG_HID0 = 0x77
REG_HID1 = 0x78


class GdbError(RuntimeError):
    pass


class GdbClient:
    def __init__(self, host: str, port: int, connect_timeout: float = 60.0) -> None:
        deadline = time.monotonic() + connect_timeout
        while True:
            try:
                self.sock = socket.create_connection((host, port), timeout=5.0)
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.2)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.sock.settimeout(None)
        self.buf = b""

    # --- transporte -------------------------------------------------------------------------------------------------
    def _read_byte(self) -> int:
        while not self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise GdbError("conexão fechada pelo Dolphin")
            self.buf += chunk
        b = self.buf[0]
        self.buf = self.buf[1:]
        return b

    def _read_packet(self) -> str:
        while True:
            c = self._read_byte()
            if c == ord("$"):
                break
            # '+' (ack) e '-' são ignorados aqui; um '-' faria o reenvio, mas o stub não o pede em loopback.
        data = bytearray()
        while (c := self._read_byte()) != ord("#"):
            data.append(c)
        chk = int(chr(self._read_byte()) + chr(self._read_byte()), 16)
        if (sum(data) & 0xFF) != chk:
            self.sock.sendall(b"-")
            raise GdbError("checksum inválido na resposta")
        self.sock.sendall(b"+")
        return data.decode("ascii")

    def _send(self, payload: str) -> None:
        raw = payload.encode("ascii")
        self.sock.sendall(b"$" + raw + b"#" + f"{sum(raw) & 0xFF:02x}".encode("ascii"))

    def command(self, payload: str) -> str:
        self._send(payload)
        return self._read_packet()

    # --- operações --------------------------------------------------------------------------------------------------
    def read_reg(self, reg_id: int) -> int:
        reply = self.command(f"p{reg_id:x}")
        if reply.startswith("E") and len(reply) == 3:
            raise GdbError(f"p{reg_id:x}: {reply}")
        return int(reply, 16)

    def read_mem(self, addr: int, size: int) -> bytes:
        out = bytearray()
        while size > 0:
            n = min(size, 4096)
            reply = self.command(f"m{addr:x},{n:x}")
            if reply.startswith("E") and len(reply) == 3:
                raise GdbError(f"m{addr:x},{n:x}: {reply}")
            out += bytes.fromhex(reply)
            addr += n
            size -= n
        return bytes(out)

    def add_breakpoint(self, addr: int) -> None:
        reply = self.command(f"Z0,{addr:x},4")
        if reply != "OK":
            raise GdbError(f"Z0 {addr:08x}: {reply}")

    def remove_breakpoint(self, addr: int) -> None:
        self.command(f"z0,{addr:x},4")

    def cont(self) -> str:
        """Continua e devolve o pacote de parada (ex.: 'S05')."""
        return self.command("c")

    def kill(self) -> None:
        try:
            self._send("k")
        except OSError:
            pass
        self.sock.close()
