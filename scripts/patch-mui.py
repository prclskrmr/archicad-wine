#!/usr/bin/env python3
"""Redireciona o import de KERNEL32.dll para rsproxy.dll (patch de 11 bytes).

O Archicad carrega os textos/recursos das *satellite DLLs* (.mui), mas o Wine
nao tem o fallback de MUI que o Windows tem. A solucao e fazer RS.dll e
GSRoot.dll importarem o nosso proxy no lugar do KERNEL32: trocamos o NOME da
DLL no descritor de import. Como "KERNEL32.dll" e "rsproxy.dll" tem o mesmo
tamanho (12 bytes + NUL), nenhum offset do PE muda.

Uso:
    patch-mui.py RS.dll GSRoot.dll
    patch-mui.py --restore RS.dll GSRoot.dll
    patch-mui.py --dry-run RS.dll
    patch-mui.py --check RS.dll          # so informa o estado

Seguranca: cria <arquivo>.orig antes da primeira modificacao e nunca
sobrescreve um .orig existente.
"""

import argparse
import os
import struct
import sys

OLD = b"KERNEL32.dll"
NEW = b"rsproxy.dll"
assert len(NEW) + 1 <= len(OLD) + 1, "NEW precisa caber em OLD"


class PE:
    def __init__(self, data):
        self.data = data
        if data[:2] != b"MZ":
            raise ValueError("nao e um PE (falta assinatura MZ)")
        self.pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[self.pe:self.pe + 4] != b"PE\0\0":
            raise ValueError("nao e um PE (falta assinatura PE)")
        self.nsec = struct.unpack_from("<H", data, self.pe + 6)[0]
        optsz = struct.unpack_from("<H", data, self.pe + 20)[0]
        self.opt = self.pe + 24
        magic = struct.unpack_from("<H", data, self.opt)[0]
        if magic == 0x20B:
            self.dd = self.opt + 112
        elif magic == 0x10B:
            self.dd = self.opt + 96
        else:
            raise ValueError("formato PE desconhecido (magic=0x%x)" % magic)
        self.sections = []
        s = self.opt + optsz
        for _ in range(self.nsec):
            va, rawsz, rawptr = struct.unpack_from("<III", data, s + 12)
            self.sections.append((va, rawsz, rawptr))
            s += 40

    def rva2off(self, rva):
        for va, rawsz, rawptr in self.sections:
            if va <= rva < va + max(rawsz, 1):
                return rawptr + (rva - va)
        return None

    def cstr(self, rva):
        off = self.rva2off(rva)
        if off is None:
            return None, None
        end = self.data.find(b"\0", off)
        if end < 0:
            return None, None
        return off, self.data[off:end]

    def import_descriptors(self):
        """Devolve [(indice, offset_no_arquivo, nome_bytes)] de cada descritor."""
        off = self.rva2off(struct.unpack_from("<I", self.data, self.dd + 8)[0])
        if off is None:
            return []
        out = []
        i = 0
        while True:
            ent = off + i * 20
            if ent + 20 > len(self.data):
                break
            ilt, ts, fc, name_rva, iat = struct.unpack_from("<IIIII", self.data, ent)
            if not (ilt or ts or fc or name_rva or iat):
                break
            s_off, name = self.cstr(name_rva)
            out.append((i, s_off, name))
            i += 1
        return out


def show(path):
    with open(path, "rb") as f:
        pe = PE(f.read())
    print(f"{path}:")
    for i, off, name in pe.import_descriptors():
        mark = ""
        if name and name.upper() == NEW.upper():
            mark = "  <- ja aponta para rsproxy"
        elif name and name.upper() == OLD.upper():
            mark = "  <- sera redirecionado"
        print(f"  [{i}] {name.decode('latin-1') if name else '?'}{mark}")


def patch(path, restore=False, dry_run=False):
    with open(path, "rb") as f:
        original = f.read()
    pe = PE(original)
    descs = pe.import_descriptors()

    if restore:
        bak = path + ".orig"
        if not os.path.exists(bak):
            print(f"  ! {path}: sem backup .orig, nada a restaurar")
            return False
        if dry_run:
            print(f"  - {path}: restauraria de {bak}")
            return True
        with open(bak, "rb") as f:
            data = f.read()
        with open(path, "wb") as f:
            f.write(data)
        print(f"  ok {path}: restaurado de {os.path.basename(bak)}")
        return True

    target = [(i, off, name) for i, off, name in descs
              if name and name.upper() == OLD.upper()]
    if not target:
        if any(name and name.upper() == NEW.upper() for _, _, name in descs):
            print(f"  = {path}: ja aponta para rsproxy.dll")
            return True
        print(f"  ! {path}: nenhum import de KERNEL32.dll encontrado")
        return False

    if len(target) > 1:
        print(f"  ! {path}: {len(target)} descritores KERNEL32; "
              f"usando o primeiro (offset {target[0][1]})")

    idx, off, name = target[0]
    old_field = original[off:off + len(OLD) + 1]
    new_field = NEW + b"\0" * (len(old_field) - len(NEW))
    if len(new_field) != len(old_field):
        print(f"  ! {path}: tamanho incompativel, abortando")
        return False

    print(f"  {path}: descritor [{idx}] offset 0x{off:X} ({off})")
    print(f"    antes : {old_field}")
    print(f"    depois: {new_field}")
    if dry_run:
        return True

    if not os.path.exists(path + ".orig"):
        with open(path + ".orig", "wb") as f:
            f.write(original)
        print(f"    backup: {os.path.basename(path)}.orig")

    data = bytearray(original)
    data[off:off + len(new_field)] = new_field
    with open(path, "wb") as f:
        f.write(data)
    print("    aplicado")
    return True


def main():
    ap = argparse.ArgumentParser(
        description="Redireciona imports KERNEL32.dll -> rsproxy.dll")
    ap.add_argument("files", nargs="+", help="DLLs a modificar")
    ap.add_argument("--restore", action="store_true", help="restaura os .orig")
    ap.add_argument("--dry-run", action="store_true", help="nao grava nada")
    ap.add_argument("--check", action="store_true", help="so mostra os imports")
    args = ap.parse_args()

    rc = 0
    for path in args.files:
        if not os.path.isfile(path):
            print(f"  ! {path}: nao existe")
            rc = 1
            continue
        try:
            if args.check:
                show(path)
            elif not patch(path, restore=args.restore, dry_run=args.dry_run):
                rc = 1
        except (ValueError, struct.error) as e:
            print(f"  ! {path}: {e}")
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
