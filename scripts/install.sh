#!/usr/bin/env bash
# install.sh — instala o Archicad 29 no Wine com as correcoes do archicad-wine.
#
# Etapas:
#   1. confere dependencias (wine, winetricks, python3)
#   2. cria o prefixo win64
#   3. instala vcrun2022 + corefonts no prefixo
#   4. roda o instalador do Archicad (se --installer e ainda nao instalado)
#   5. instala o rsproxy.dll (dist/ pronta ou compila com --build)
#   6. aplica o import redirect em RS.dll / GSRoot.dll
#   7. grava as chaves de registro do Graphisoft
#   8. instala o launcher em ~/.local/bin/archicad29
#
# Uso:
#   ./scripts/install.sh --installer ~/Downloads/Archicad-29-INT.exe
#   ./scripts/install.sh                 # aplica so as correcoes
#
# Opcoes:
#   --installer PATH    instalador .exe do Archicad (abre em modo grafico)
#   --prefix DIR        WINEPREFIX (default ~/.local/share/wineprefixes/archicad29)
#   --build             compila o rsproxy do fonte (precisa mingw-w64)
#   --skip-winetricks   nao roda winetricks (prefixo ja preparado)
#   --yes               nao pede confirmacao
#   -h, --help          esta ajuda

set -euo pipefail

REPO="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
PREFIX="${WINEPREFIX:-$HOME/.local/share/wineprefixes/archicad29}"
DATA_DIR="${ARCHICAD_WINE_DATA:-$HOME/.local/share/archicad-wine}"
INSTALLER=""
DO_BUILD=0
SKIP_WT=0
ASSUME_YES=0

msg()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
ok()   { printf '\033[1;32m  ok\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m  !!\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31merro:\033[0m %s\n' "$*" >&2; exit 1; }

usage() { sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0; }

while [ $# -gt 0 ]; do
    case "$1" in
        --installer) INSTALLER="${2:-}"; shift 2 ;;
        --prefix)    PREFIX="${2:-}"; shift 2 ;;
        --build)     DO_BUILD=1; shift ;;
        --skip-winetricks) SKIP_WT=1; shift ;;
        --yes|-y)    ASSUME_YES=1; shift ;;
        -h|--help)   usage ;;
        *) die "opcao desconhecida: $1 (use --help)" ;;
    esac
done

export WINEPREFIX="$PREFIX"
export WINEARCH=win64
export WINEDEBUG="${WINEDEBUG:--all}"
AC="$PREFIX/drive_c/Program Files/Graphisoft/Archicad 29"

# ---------------------------------------------------------------- 0. distro
detect_distro() {
    [ -r /etc/os-release ] || { echo "unknown"; return; }
    # shellcheck disable=SC1091
    . /etc/os-release
    case " ${ID:-} ${ID_LIKE:-} " in
        *" arch "*|*" manjaro "*) echo "arch" ;;
        *" debian "*|*" ubuntu "*) echo "debian" ;;
        *" fedora "*|*" rhel "*) echo "fedora" ;;
        *" suse "*|*" opensuse "*) echo "suse" ;;
        *) echo "unknown" ;;
    esac
}
DISTRO="$(detect_distro)"

hint_pkg() {
    case "$DISTRO" in
        arch)   echo "  sudo pacman -S --needed $*" ;;
        debian) echo "  sudo apt install $*" ;;
        fedora) echo "  sudo dnf install $*" ;;
        suse)   echo "  sudo zypper install $*" ;;
        *)      echo "  instale: $*" ;;
    esac
}

# ---------------------------------------------------------------- 1. deps
msg "Verificando dependencias (distro detectada: $DISTRO)"
missing=0
for cmd in wine python3; do
    command -v "$cmd" >/dev/null 2>&1 || { warn "falta: $cmd"; missing=1; }
done
if [ "$SKIP_WT" = 0 ] && ! command -v winetricks >/dev/null 2>&1; then
    warn "falta: winetricks"
    missing=1
fi
if [ "$missing" = 1 ]; then
    printf '\nInstale com:\n'
    case "$DISTRO" in
        arch)   hint_pkg "wine winetricks python" ;;
        debian) hint_pkg "winehq-staging winetricks python3" ;;
        *)      hint_pkg "wine winetricks python3" ;;
    esac
    echo "  (detalhes por distro: docs/INSTALL.md)"
    die "dependencias faltando"
fi

WINEPATH_BIN="$(command -v wine)"
"$WINEPATH_BIN" --version
ok "wine: $("$WINEPATH_BIN" --version)"

# ---------------------------------------------------------------- 2. prefixo
if [ -d "$PREFIX" ]; then
    ok "prefixo existente: $PREFIX"
else
    msg "Criando prefixo win64 em $PREFIX"
    wineboot -u >/dev/null 2>&1
    ok "prefixo criado"
fi

# ---------------------------------------------------------------- 3. runtime
if [ "$SKIP_WT" = 0 ]; then
    msg "Instalando runtime VC++ 2022 e fontes no prefixo (pode demorar)"
    winetricks --unattended vcrun2022 corefonts || \
        warn "winetricks retornou erro; se o Archicad abrir sem texto/icones, rode manualmente:
  WINEPREFIX=\"$PREFIX\" winetricks vcrun2022 corefonts"
    ok "runtime/fontes"
else
    ok "winetricks ignorado (--skip-winetricks)"
fi

# ---------------------------------------------------------------- 4. archicad
if [ -f "$AC/Archicad.exe" ]; then
    ok "Archicad 29 ja instalado em $AC"
elif [ -n "$INSTALLER" ]; then
    [ -f "$INSTALLER" ] || die "instalador nao encontrado: $INSTALLER"
    msg "Abrindo o instalador do Archicad (siga Next/Next; pode levar minutos)"
    wine "$(realpath "$INSTALLER")" || die "instalador terminou com erro"
    [ -f "$AC/Archicad.exe" ] || die "instalacao nao encontrada em $AC depois do instalador"
    ok "Archicad instalado"
else
    warn "Archicad nao esta instalado e nenhum --installer foi passado"
    echo "  rode de novo com:  $0 --installer /caminho/Archicad-29-INT.exe"
    exit 1
fi

# ---------------------------------------------------------------- 5. rsproxy
msg "Instalando rsproxy.dll"
mkdir -p "$DATA_DIR"
PROXY=""

if [ "$DO_BUILD" = 1 ]; then
    command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 || {
        warn "mingw-w64 nao encontrado"
        printf 'Instale com: '; hint_pkg "mingw-w64-gcc"
        die "nao posso compilar"
    }
    msg "Compilando src/rsproxy"
    make -C "$REPO/src/rsproxy" clean all >/dev/null
    PROXY="$REPO/src/rsproxy/rsproxy.dll"
    ok "compilado: $PROXY"
else
    for cand in "$REPO/dist/rsproxy.dll" "$REPO/src/rsproxy/rsproxy.dll"; do
        [ -f "$cand" ] && { PROXY="$cand"; break; }
    done
    [ -n "$PROXY" ] || {
        warn "dist/rsproxy.dll ausente; compilando do fonte"
        make -C "$REPO/src/rsproxy" all >/dev/null
        PROXY="$REPO/src/rsproxy/rsproxy.dll"
    }
    ok "usando binario testado: $PROXY"
fi

cp -f "$PROXY" "$DATA_DIR/rsproxy.dll"
[ -f "$AC/rsproxy.dll" ] && cp -f "$AC/rsproxy.dll" "$AC/rsproxy.dll.prev" 2>/dev/null || true
cp -f "$PROXY" "$AC/rsproxy.dll"
ok "rsproxy.dll em $AC"

# ---------------------------------------------------------------- 6. patch
msg "Aplicando import redirect em RS.dll e GSRoot.dll"
for dll in RS.dll GSRoot.dll; do
    [ -f "$AC/$dll" ] || die "nao encontrei $dll em $AC"
    python3 "$REPO/scripts/patch-mui.py" "$AC/$dll" | sed 's/^/    /'
done
ok "import redirect"

# ---------------------------------------------------------------- 7. registro
msg "Gravando chaves de registro do Graphisoft"
wine reg add 'HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Bug reporting' \
     /v disableIncoherentSharedLibraryAlert /t REG_DWORD /d 1 /f >/dev/null
wine reg add 'HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Bug reporting' \
     /v disableOOPReporting /t REG_DWORD /d 1 /f >/dev/null
ok "bug reporting silenciado"

# ---------------------------------------------------------------- 8. launcher
msg "Instalando launcher"
BIN_DIR="${BIN_DIR:-$HOME/.local/bin}"
mkdir -p "$BIN_DIR"
install -m 755 "$REPO/scripts/archicad29.sh" "$BIN_DIR/archicad29"
ok "$BIN_DIR/archicad29"

# ---------------------------------------------------------------- 9. resumo
cat <<EOF

------------------------------------------------------------
 Instalacao concluida.

  prefixo : $PREFIX
  archicad: $AC
  proxy   : $AC/rsproxy.dll  (backup: rsproxy.dll.prev)
  original: $AC/RS.dll.orig, $AC/GSRoot.dll.orig

 Abrir:
   $BIN_DIR/archicad29
   $BIN_DIR/archicad29 'Z:\\home\\$USER\\Downloads\\projeto.pln'

 Primeira abertura leva 2-4 min (licenca, add-ons, bibliotecas).
 Verificar: tools/acstate.sh   |   Problemas: docs/TROUBLESHOOTING.md
 Reverter  : ./scripts/uninstall.sh
------------------------------------------------------------
EOF

case ":$PATH:" in
    *":$BIN_DIR:"*) ;;
    *) warn "$BIN_DIR nao esta no PATH — use o caminho completo ou adicione ao shell" ;;
esac
