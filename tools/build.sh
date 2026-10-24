#!/usr/bin/env bash
# Baut die Hilfsprogramme in tools/ mit MinGW (x86_64-w64-mingw32-gcc aus 05-build-deps.sh).
# Noetig fuer den Nachbau ist nur psgetproc.exe (BIMcloud; 15-bimcloud.sh ps-an baut es bei Bedarf selbst).
# Die anderen sind Diagnosewerkzeuge (nur lesend). Aufruf: bash tools/build.sh
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
CC=x86_64-w64-mingw32-gcc
$CC -O1 -o psgetproc.exe psgetproc.c
$CC -O2 -o winlist.exe winlist.c
$CC -O1 -o dwsize.exe dwsize.c -ldwrite
$CC -O1 -o rgndump.exe rgndump.c -luser32 -lgdi32
$CC -O1 -o dlgpaint.exe dlgpaint.c -luser32 -lgdi32
$CC -O1 -o tomtest.exe tomtest.c -luser32 -lgdi32 -lole32 -loleaut32 -luuid
ls -l *.exe
