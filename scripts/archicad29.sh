#!/usr/bin/env bash
# archicad29 — launcher do Archicad 29 no Wine.
#
# Faz tres coisas antes de subir o programa:
#   1. garante que o rsproxy.dll do prefixo e o mesmo do repositorio/payload
#      (mantendo o anterior como rsproxy.dll.prev);
#   2. garante as chaves de registro do Graphisoft (pastas do usuario,
#      silenciamento do alerta de DLL e do supervisor de bug report);
#   3. cria a arvore de pastas do GS que o Wine nao cria sozinho.
#
# Uso:
#   archicad29                      # tela inicial
#   archicad29 'Z:\home\me\a.pln'   # abre projeto
#
# Variaveis uteis:
#   WINEPREFIX   (default: ~/.local/share/wineprefixes/archicad29)
#   ARCHICAD_DIR (default: <prefixo>/drive_c/Program Files/Graphisoft/Archicad 29)
#   ARCHICAD_WINE_DATA (default: ~/.local/share/archicad-wine)
#   RSPROXY_VERBOSE=1 / RSPROXY_SANITIZE=1  (ver docs/PATCHES.md)

set -u

export WINEPREFIX="${WINEPREFIX:-$HOME/.local/share/wineprefixes/archicad29}"
export WINEARCH=win64
export WINEDEBUG="${WINEDEBUG:--all}"

# Fontconfig: evita fallback para fontes sem glifos latinos (ex.: Droid Sans Fallback)
REPO_DIR_FONT="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
if [ -z "${FONTCONFIG_FILE:-}" ] && [ -f "$REPO_DIR_FONT/fontconfig-archicad.conf" ]; then
    export FONTCONFIG_FILE="$REPO_DIR_FONT/fontconfig-archicad.conf"
fi

# AMD: algumas RDNA2 precisam do override para o ROCm/OpenGL; inofensivo em
# outras GPUs (so afeta bibliotecas ROCm/HSA).
: "${HSA_OVERRIDE_GFX_VERSION:=10.3.0}"
export HSA_OVERRIDE_GFX_VERSION

DATA_DIR="${ARCHICAD_WINE_DATA:-$HOME/.local/share/archicad-wine}"
AC="${ARCHICAD_DIR:-$WINEPREFIX/drive_c/Program Files/Graphisoft/Archicad 29}"

# handle do usuario do prefixo (normalmente o mesmo do Linux)
WUSER="${WINEUSER:-$USER}"
GSVER="29.0.0 INT R1"
GSBASE="$WINEPREFIX/drive_c/users/$WUSER/AppData/Local/Graphisoft/ $GSVER"

die() { printf 'archicad29: %s\n' "$*" >&2; exit 1; }

[ -d "$AC" ] || die "instalacao nao encontrada em: $AC
Rode scripts/install.sh (ou defina ARCHICAD_DIR)."
[ -f "$AC/Archicad.exe" ] || die "Archicad.exe nao encontrado em: $AC"

# --- 1. rsproxy.dll -------------------------------------------------------
payload=""
for cand in "$DATA_DIR/rsproxy.dll" \
            "$(dirname "$(readlink -f "$0")")/../dist/rsproxy.dll" \
            "$(dirname "$(readlink -f "$0")")/rsproxy.dll"; do
    [ -f "$cand" ] && { payload="$cand"; break; }
done

if [ -n "$payload" ] && ! cmp -s "$payload" "$AC/rsproxy.dll"; then
    [ -f "$AC/rsproxy.dll" ] && cp -f "$AC/rsproxy.dll" "$AC/rsproxy.dll.prev" 2>/dev/null
    cp -f "$payload" "$AC/rsproxy.dll" || die "falha ao copiar rsproxy.dll"
    echo "archicad29: rsproxy.dll atualizado (anterior: rsproxy.dll.prev)"
elif [ ! -f "$AC/rsproxy.dll" ]; then
    die "rsproxy.dll ausente em $AC e nenhum payload encontrado.
Rode scripts/install.sh para instalar o proxy."
fi

# --- 2. registro ----------------------------------------------------------
# Falha silenciosa se o wine ainda nao inicializou o prefixo; wineboot cuida.
wine reg query 'HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Bug reporting' \
     /v disableOOPReporting >/dev/null 2>&1 || {
    wine reg add 'HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Bug reporting' \
         /v disableIncoherentSharedLibraryAlert /t REG_DWORD /d 1 /f >/dev/null 2>&1
    wine reg add 'HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Bug reporting' \
         /v disableOOPReporting /t REG_DWORD /d 1 /f >/dev/null 2>&1
}

# pastas do GS (a chave tem um espaco antes da versao; e assim mesmo)
if ! wine reg query 'HKCU\Software\GRAPHISOFT\ 29.0.0 INT R1\Special Folder Locations' \
        /v 'Data Folder' >/dev/null 2>&1; then
    key='HKCU\Software\GRAPHISOFT\ 29.0.0 INT R1\Special Folder Locations'
    base="C:\\users\\$WUSER\\AppData\\Local\\Graphisoft\\ $GSVER"
    docs="C:\\users\\$WUSER\\Documents\\Graphisoft\\ $GSVER"
    wine reg add "$key" /v 'User Folder'       /t REG_SZ /d "$docs"                /f >/dev/null 2>&1
    wine reg add "$key" /v 'Cache Folder'      /t REG_SZ /d "$base\\Cache"         /f >/dev/null 2>&1
    wine reg add "$key" /v 'Log Folder'        /t REG_SZ /d "$base\\Logs"          /f >/dev/null 2>&1
    wine reg add "$key" /v 'Temporary Folder'  /t REG_SZ /d "C:\\users\\$WUSER\\AppData\\Local\\Temp" /f >/dev/null 2>&1
    wine reg add "$key" /v 'Data Folder'       /t REG_SZ /d "$base"                /f >/dev/null 2>&1
    wine reg add "$key" /v 'AutoSave Folder'   /t REG_SZ /d "$base\\AutoSave"      /f >/dev/null 2>&1
    wine reg add "$key" /v 'Web Objects Folder' /t REG_SZ /d "$base\\WebObjects"   /f >/dev/null 2>&1
    wine reg add "$key" /v 'Templates Folder'  /t REG_SZ /d "$base\\Templates"     /f >/dev/null 2>&1
fi

# --- 3. pastas ------------------------------------------------------------
mkdir -p "$GSBASE"/{Cache,Logs,AutoSave,WebObjects,Templates}
mkdir -p "$WINEPREFIX/drive_c/users/$WUSER/Documents/Graphisoft/ $GSVER"

cd "$AC" || die "nao consegui entrar em $AC"
exec wine "Archicad.exe" "$@"
