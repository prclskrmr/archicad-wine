#!/usr/bin/env python3
"""Le um minidump (.dmp) do Graphisoft/Wine e mostra a pilha provavel.

O Graphisoft grava `CrashDump-*.dmp` em
`drive_c/users/<user>/Graphisoft/BugReporting-29/`. Nem sempre sai um `.RPT`
junto (quando o crash e no meio do reportador), entao o dump e a unica pista.

Nao faz unwinding de verdade (isso exigiria as tabelas .pdata de cada modulo,
que o minidump pode nao trazer). Em vez disso:
  1. le a excecao (codigo, endereco, thread);
  2. le os registradores da thread (RIP/RSP/RBP);
  3. varre a pilha procurando valores que caiam dentro de um modulo carregado
     e imprime `modulo + offset` em ordem de endereco (pilha cresce para baixo,
     entao endereco crescente = ordem de chamada).

Uso:
    tools/dmpstack.py CrashDump-....dmp
    tools/dmpstack.py --modules CrashDump-....dmp     # so a lista de modulos
    tools/dmpstack.py --all CrashDump-....dmp         # todas as threads

Sem dependencias externas.
"""

import struct
import sys

# tipos de stream
THREAD_LIST = 3
MODULE_LIST = 4
MEMORY_LIST = 5
EXCEPTION = 6
MEMORY64_LIST = 9

EXCEPTION_NAMES = {
    0xC0000005: "ACCESS_VIOLATION",
    0xC000001D: "ILLEGAL_INSTRUCTION",
    0xC0000094: "INT_DIVIDE_BY_ZERO",
    0xC00000FD: "STACK_OVERFLOW",
    0xC0000409: "STACK_BUFFER_OVERRUN (fastfail)",
    0x80000003: "BREAKPOINT",
    0xC0000135: "DLL_NOT_FOUND",
    0xC0000142: "DLL_INIT_FAILED",
    0xE06D7363: "MSVC++ exception",
}


class Dump:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.buf = f.read()
        b = self.buf
        if b[:4] != b"MDMP":
            raise SystemExit("nao e um minidump (assinatura MDMP ausente)")
        nstreams, dir_rva = struct.unpack_from("<II", b, 8)
        self.streams = {}
        for i in range(nstreams):
            stype, size, rva = struct.unpack_from("<III", b, dir_rva + i * 12)
            self.streams.setdefault(stype, (size, rva))

    def u32(self, off):
        return struct.unpack_from("<I", self.buf, off)[0]

    def u64(self, off):
        return struct.unpack_from("<Q", self.buf, off)[0]

    def string(self, rva):
        if not rva:
            return ""
        n = self.u32(rva)
        raw = self.buf[rva + 4:rva + 4 + n]
        return raw.decode("utf-16le", "replace")

    # -- streams -------------------------------------------------------
    def modules(self):
        if MODULE_LIST not in self.streams:
            return []
        _, rva = self.streams[MODULE_LIST]
        n = self.u32(rva)
        out = []
        for i in range(n):
            e = rva + 4 + i * 108
            base = self.u64(e)
            size = self.u32(e + 8)
            name = self.string(self.u32(e + 20))
            out.append((base, size, name))
        return out

    def threads(self):
        if THREAD_LIST not in self.streams:
            return []
        _, rva = self.streams[THREAD_LIST]
        n = self.u32(rva)
        out = []
        for i in range(n):
            e = rva + 4 + i * 48
            tid = self.u32(e)
            tcb = self.u64(e + 16)
            stack_start = self.u64(e + 24)
            stack_size = self.u32(e + 32)
            stack_rva = self.u32(e + 36)
            ctx_size = self.u32(e + 40)
            ctx_rva = self.u32(e + 44)
            out.append({
                "tid": tid, "teb": tcb, "stack_start": stack_start,
                "stack_size": stack_size, "stack_rva": stack_rva,
                "ctx_size": ctx_size, "ctx_rva": ctx_rva,
            })
        return out

    def exception(self):
        if EXCEPTION not in self.streams:
            return None
        _, rva = self.streams[EXCEPTION]
        tid = self.u32(rva)
        code = self.u32(rva + 8)
        flags = self.u32(rva + 12)
        addr = self.u64(rva + 24)
        nparams = self.u32(rva + 32)
        params = [self.u64(rva + 40 + i * 8) for i in range(min(nparams, 15))]
        ctx_size = self.u32(rva + 160)
        ctx_rva = self.u32(rva + 164)
        return {
            "tid": tid, "code": code, "flags": flags, "address": addr,
            "params": params, "ctx_size": ctx_size, "ctx_rva": ctx_rva,
        }

    def memory_ranges(self):
        """Lista de (inicio, tamanho, rva_dos_dados)."""
        out = []
        if MEMORY_LIST in self.streams:
            _, rva = self.streams[MEMORY_LIST]
            n = self.u32(rva)
            for i in range(n):
                e = rva + 4 + i * 16
                out.append((self.u64(e), self.u32(e + 8), self.u32(e + 12)))
        if MEMORY64_LIST in self.streams:
            _, rva = self.streams[MEMORY64_LIST]
            n = self.u64(rva)
            base_rva = self.u64(rva + 8)
            off = base_rva
            for i in range(n):
                e = rva + 16 + i * 16
                start = self.u64(e)
                size = self.u64(e + 8)
                out.append((start, size, off))
                off += size
        return out


def ctx_regs(dump, ctx_rva):
    """RIP/RSP/RBP de um CONTEXT x64 (1232 bytes)."""
    if not ctx_rva:
        return None
    return {
        "rip": dump.u64(ctx_rva + 0xF8),
        "rsp": dump.u64(ctx_rva + 0x98),
        "rbp": dump.u64(ctx_rva + 0xA0),
    }


def find_module(mods, addr):
    for base, size, name in mods:
        if base <= addr < base + size:
            return name, addr - base
    return None, None


def read_stack(dump, ranges, start, size):
    """Le a pilha juntando os pedacos que existem no dump."""
    data = {}
    for rstart, rsize, rrva in ranges:
        lo = max(rstart, start)
        hi = min(rstart + rsize, start + size)
        if lo < hi:
            data[lo] = dump.buf[rrva + (lo - rstart):rrva + (hi - rstart)]
    return data


def scan_frames(dump, mods, ranges, start, size, limit=40):
    """Percorre a pilha de baixo para cima (ordem de chamada) e anota os
    qwords que apontam para dentro de modulos."""
    frames = []
    segs = sorted(read_stack(dump, ranges, start, size).items())
    for base, blob in segs:
        for off in range(0, len(blob) - 7, 8):
            val = struct.unpack_from("<Q", blob, off)[0]
            name, delta = find_module(mods, val)
            if name:
                frames.append((base + off, name, delta))
    return frames[-limit:]


def basename(p):
    return p.replace("\\", "/").rsplit("/", 1)[-1]


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    show_all = "--all" in argv
    only_mods = "--modules" in argv
    path = [a for a in argv if not a.startswith("--")][0]

    d = Dump(path)
    mods = d.modules()
    ranges = d.memory_ranges()

    if only_mods:
        for base, size, name in sorted(mods):
            print(f"  {base:016x}-{base + size:016x} {basename(name)}")
        return 0

    print(f"dump: {path}  ({len(d.buf)} bytes, {len(mods)} modulos)")
    exc = d.exception()
    threads = d.threads()
    by_tid = {t["tid"]: t for t in threads}

    if exc:
        name, delta = find_module(mods, exc["address"])
        label = EXCEPTION_NAMES.get(exc["code"], hex(exc["code"]))
        print(f"\nexcecao: {label} (0x{exc['code']:08X})")
        print(f"  endereco: 0x{exc['address']:016x}"
              + (f"  -> {basename(name)}+{delta:#x}" if name else ""))
        if exc["params"]:
            print("  parametros: " + ", ".join(f"0x{p:x}" for p in exc["params"]))
        tid = exc["tid"]
    else:
        tid = threads[0]["tid"] if threads else None

    def show_thread(t, header):
        regs = ctx_regs(d, t["ctx_rva"])
        print(f"\n{header} (tid {t['tid']}, teb 0x{t['teb']:x})")
        if regs:
            name, delta = find_module(mods, regs["rip"])
            rip = f"0x{regs['rip']:016x}"
            if name:
                rip += f" {basename(name)}+{delta:#x}"
            print(f"  rip={rip}  rsp=0x{regs['rsp']:016x}  rbp=0x{regs['rbp']:016x}")
            start = regs["rsp"]
        else:
            start = t["stack_start"]
        print("  pilha (endereco crescente = ordem de chamada):")
        for addr, name, delta in scan_frames(d, mods, ranges, start, 0x20000):
            print(f"    sp+0x{addr - start:05x}  {basename(name):24s} + {delta:#08x}")

    if exc and tid in by_tid:
        show_thread(by_tid[tid], "thread da excecao")
    if show_all:
        for t in threads:
            if not exc or t["tid"] != tid:
                show_thread(t, "thread")
    elif not exc:
        for t in threads[:1]:
            show_thread(t, "thread principal")

    if not exc:
        print("\n(sem stream de excecao; mostrando a primeira thread)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
