#!/usr/bin/env python3
"""Lista (e opcionalmente extrai) recursos de arquivos PE.

Uso:
    peicons.py <dir>                 # resumo por arquivo: ICON/BITMAP/GROUP_ICON
    peicons.py --types <arquivo...>  # todos os tipos + contagem
    peicons.py --dump <arquivo> <dir-de-saida>   # extrai RT_ICON/RT_GROUP_ICON
    peicons.py --groups <arquivo>    # lista grupos de icones (nome -> membros)

Sem dependencias externas: le a arvore de recursos do PE na mao.
"""

import collections
import os
import struct
import sys

TYPE_NAMES = {
    1: "CURSOR", 2: "BITMAP", 3: "ICON", 4: "MENU", 5: "DIALOG", 6: "STRING",
    7: "FONTDIR", 8: "FONT", 9: "ACCEL", 10: "RCDATA", 11: "MSGTABLE",
    12: "GROUP_CURSOR", 14: "GROUP_ICON", 16: "VERSION", 24: "MANIFEST",
}


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


class PE:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = data = f.read()
        if data[:2] != b"MZ":
            raise ValueError("nao e PE")
        pe = u32(data, 0x3C)
        if data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("nao e PE")
        self.nsec = u16(data, pe + 6)
        optsz = u16(data, pe + 20)
        self.opt = pe + 24
        magic = u16(data, self.opt)
        dd = self.opt + (112 if magic == 0x20B else 96)
        self.rsrc_rva = u32(data, dd + 16)
        self.sections = []
        s = self.opt + optsz
        for _ in range(self.nsec):
            va, rawsz, rawptr = struct.unpack_from("<III", data, s + 12)
            self.sections.append((va, rawsz, rawptr))
            s += 40
        self.base = self.off(self.rsrc_rva) if self.rsrc_rva else None
        if self.base is None:
            raise ValueError("sem secao de recursos")

    def off(self, rva):
        for va, rawsz, rawptr in self.sections:
            if va <= rva < va + rawsz:
                return rawptr + (rva - va)
        return None

    def _name(self, nameid):
        if nameid & 0x80000000:
            so = self.base + (nameid & 0x7FFFFFFF)
            if so + 2 > len(self.data):
                return "?"
            ln = u16(self.data, so)
            return self.data[so + 2:so + 2 + ln * 2].decode("utf-16le", "replace")
        return nameid

    def entries(self):
        """Gera (tipo, nome, lang, size, rva) para cada folha da arvore."""
        data = self.data

        def walk(o, depth, tname, name):
            if o + 16 > len(data):
                return
            nn, ni = u16(data, o + 12), u16(data, o + 14)
            for i in range(nn + ni):
                e = o + 16 + i * 8
                if e + 8 > len(data):
                    return
                nameid, target = u32(data, e), u32(data, e + 4)
                nm = self._name(nameid)
                sub = self.base + (target & 0x7FFFFFFF)
                if depth < 2:
                    yield from walk(sub, depth + 1,
                                    nm if depth == 0 else tname,
                                    nm if depth == 1 else name)
                else:
                    if sub + 16 > len(data):
                        continue
                    yield (tname, name, nm, u32(data, sub + 4), u32(data, sub))

        yield from walk(self.base, 0, None, None)


def tname(t):
    if isinstance(t, str):
        return t
    return TYPE_NAMES.get(t, str(t))


def cmd_scan(root):
    total = collections.Counter()
    rows = []
    for dp, _, fn in os.walk(root):
        for f in fn:
            if not f.lower().endswith((".dll", ".exe", ".apx", ".gmx")):
                continue
            p = os.path.join(dp, f)
            try:
                pe = PE(p)
            except (ValueError, struct.error, OSError):
                continue
            c = collections.Counter(tname(t) for t, _, _, _, _ in pe.entries())
            total.update(c)
            n = c["ICON"] + c["GROUP_ICON"] + c["BITMAP"]
            if n:
                rows.append((n, os.path.relpath(p, root), c))
    for n, name, c in sorted(rows, reverse=True):
        extra = " ".join(f"{k}={v}" for k, v in sorted(c.items())
                         if k in ("ICON", "GROUP_ICON", "BITMAP"))
        print(f"{n:6d}  {name}  {extra}")
    print("\n== totais:", ", ".join(f"{k}={v}" for k, v in sorted(total.items())
                                    if k in ("ICON", "BITMAP", "GROUP_ICON",
                                             "CURSOR", "GROUP_CURSOR")))


def cmd_types(files):
    for path in files:
        pe = PE(path)
        c = collections.Counter(tname(t) for t, _, _, _, _ in pe.entries())
        print(f"{os.path.basename(path)} -> ")
        for k, v in c.most_common():
            print(f"    {k:14s} {v}")


def cmd_groups(path):
    pe = PE(path)
    groups = {}
    icons = {}
    for t, name, lang, size, rva in pe.entries():
        if tname(t) == "GROUP_ICON":
            groups[name] = (size, rva)
        elif tname(t) == "ICON":
            icons[name] = (size, rva)
    print(f"{os.path.basename(path)}: {len(groups)} grupos, {len(icons)} ICON")
    for name in sorted(groups, key=lambda x: str(x))[:40]:
        size, rva = groups[name]
        o = pe.off(rva)
        cnt = u16(pe.data, o + 4) if o and o + 6 <= len(pe.data) else 0
        members = []
        for i in range(cnt):
            e = o + 6 + i * 14
            if e + 14 > len(pe.data):
                break
            w, h = pe.data[e], pe.data[e + 1]
            bpp = pe.data[e + 6] | (pe.data[e + 7] << 8)
            nid = u16(pe.data, e + 12)
            members.append(f"#{nid}({w or 256}x{h or 256},{bpp}bpp)")
        print(f"  {name}: {' '.join(members)}")


def cmd_dump(path, outdir):
    pe = PE(path)
    os.makedirs(outdir, exist_ok=True)
    n = 0
    groups = {}
    for t, name, lang, size, rva in pe.entries():
        if tname(t) == "GROUP_ICON":
            groups[name] = rva
    for t, name, lang, size, rva in pe.entries():
        if tname(t) != "ICON":
            continue
        o = pe.off(rva)
        if o is None:
            continue
        blob = pe.data[o:o + size]
        if blob[:8] == b"\x89PNG\r\n\x1a\n":
            ext = "png"
        else:
            ext = "dib"
        with open(os.path.join(outdir, f"icon_{name}.{ext}"), "wb") as f:
            f.write(blob)
        n += 1
    for name, rva in groups.items():
        o = pe.off(rva)
        if o is None:
            continue
        cnt = u16(pe.data, o + 4)
        with open(os.path.join(outdir, f"group_{name}.txt"), "w") as f:
            for i in range(cnt):
                e = o + 6 + i * 14
                w, h = pe.data[e], pe.data[e + 1]
                bpp = pe.data[e + 6] | (pe.data[e + 7] << 8)
                nid = u16(pe.data, e + 12)
                f.write(f"{nid} {w or 256}x{h or 256} {bpp}bpp\n")
    print(f"extraidos {n} RT_ICON e {len(groups)} grupos em {outdir}")


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    if argv[0] == "--dump":
        cmd_dump(argv[1], argv[2])
    elif argv[0] == "--groups":
        cmd_groups(argv[1])
    elif argv[0] == "--types":
        cmd_types(argv[1:])
    else:
        cmd_scan(argv[0])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
