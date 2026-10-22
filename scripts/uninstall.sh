#!/usr/bin/env bash
# uninstall.sh — desfaz o que o install.sh fez (sem apagar projetos).
#
# Remove: launcher, rsproxy.dll do prefixo, restaura RS.dll/GSRoot.dll dos
# .orig e (opcional) apaga as chaves de registro que criamos.
# NAO remove: a instalacao do Archicad nem o prefixo (a menos que --purge).

set -euo pipefail

REPO="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
PREFIX="${WINEPREFIX:-$HOME/.local/share/wineprefixes/archicad29}"
AC="$PREFIX/drive_c/Program Files/Graphisoft/Archicad 29"
BIN_DIR="${BIN_DIR:-$HOME/.local/bin}"
PURGE=0

[ "${1:-}" = "--purge" ] && PURGE=1

export WINEPREFIX="$PREFIX"
export WINEDEBUG=-all

echo "==> restaurando DLLs originais"
for dll in RS.dll GSRoot.dll; do
    if [ -f "$AC/$dll.orig" ]; then
        python3 "$REPO/scripts/patch-mui.py" --restore "$AC/$dll" || true
    else
        echo "  (sem $dll.orig)"
    fi
done

echo "==> removendo rsproxy"
rm -f "$AC/rsproxy.dll" "$AC/rsproxy.dll.prev"

echo "==> removendo launcher"
rm -f "$BIN_DIR/archicad29"

echo "==> removendo chaves de registro"
wine reg delete 'HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Bug reporting' /f 2>/dev/null || true

if [ "$PURGE" = 1 ]; then
    echo "==> apagando o prefixo inteiro ($PREFIX)"
    rm -rf "$PREFIX"
fi

echo "pronto."
