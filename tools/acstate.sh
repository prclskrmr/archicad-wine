#!/usr/bin/env bash
# acstate.sh — estado do Archicad sem depender de `ps comm`.
#
# O CEF renomeia a thread principal para "CrBrowserMain", entao `ps comm` e
# `pgrep Archicad` mentem. Aqui o processo e identificado pelo primeiro
# argumento exato da cmdline (/proc/<pid>/cmdline), que e "Archicad.exe".

n=0
pids=""
for p in /proc/[0-9]*; do
    [ -r "$p/cmdline" ] || continue
    c=$(tr '\0' '\n' < "$p/cmdline" 2>/dev/null | head -1)
    if [ "$c" = "Archicad.exe" ]; then
        n=$((n + 1))
        pids="$pids ${p#/proc/}"
    fi
done

if command -v xdotool >/dev/null 2>&1; then
    w=$(xdotool search --onlyvisible --name "." getwindowname %@ 2>/dev/null \
        | sort -u | tr '\n' '|')
else
    w="(xdotool ausente)"
fi

printf 'ac=%s wins=%s\n' "$n" "$w"
[ -n "$pids" ] && ps -o pid,etime,pcpu,nlwp -p ${pids} 2>/dev/null | tail -n +2
exit 0
