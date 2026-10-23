# Testes e verificação

Não existe suíte automatizada ainda (é um item do [ROADMAP](../ROADMAP.md)).
Por enquanto, este é o roteiro manual — leva ~15 minutos e cobre as quatro
correções.

---

## Smoke test (o mínimo para dizer "funciona")

```bash
export WINEPREFIX=~/.local/share/wineprefixes/archicad29
AC="$WINEPREFIX/drive_c/Program Files/Graphisoft/Archicad 29"

# 0. estado do ambiente
wine --version
ls -l "$AC/rsproxy.dll" "$AC/RS.dll.orig" "$AC/GSRoot.dll.orig"
llvm-readobj --coff-imports "$AC/RS.dll"     | grep -A0 'rsproxy.dll'
llvm-readobj --coff-imports "$AC/GSRoot.dll" | grep -A0 'rsproxy.dll'

# 1. abre um projeto pequeno
tools/acstate.sh        # antes: ac=0
archicad29 "Z:\\home\\$USER\\Downloads\\teste.pln" &
# 2. em ~4 min:
tools/acstate.sh        # ac=1 wins=Archicad 29|

# 3. na UI: desenhe uma parede, salve como .pln, feche
# 4. confira que fechou limpo:
F="$WINEPREFIX/drive_c/users/$USER/Graphisoft/Archicad Log Folder/Archicad 29.0.0 INT R1/FastLogger"
strings -n 6 "$(ls -t "$F"/*.log | head -1)" | tail -5 | grep -i 'end of shutdown'
```

Checklist:

- [ ] UI com **todos os textos** (menus, paletas, diálogos) em pt/en
- [ ] projeto abre em ≤ 4 min
- [ ] desenha/edita qualquer ferramenta
- [ ] salva `.pln` e reabre o arquivo salvo
- [ ] fecha pelo menu → log termina em `End of Shutdown`
- [ ] nenhum `.RPT` **novo** com `FATAL`

---

## Verificar cada correção isoladamente

### 1. Fallback MUI

```bash
rm -f /tmp/rsproxy.log
RSPROXY_VERBOSE=1 archicad29 &
sleep 90; grep -c '^SAT' /tmp/rsproxy.log; grep '^MISS' /tmp/rsproxy.log | wc -l
```

- `SAT > 0` → o proxy está servindo recursos dos `.mui` (bom).
- `SAT = 0` e `MISS` alto → fallback não entrou (ver TROUBLESHOOTING).
- `MISS` **sempre** será > 0: o GS consulta coisas opcionais. O que importa é
  `SAT`.

### 2. Chaves de registro

```bash
wine reg query 'HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Bug reporting'
# esperado:
#   disableIncoherentSharedLibraryAlert  REG_DWORD  0x1
#   disableOOPReporting                  REG_DWORD  0x1
```

### 3. Shim de CRT

```bash
grep -a 'crtfix' /tmp/rsproxy.log | head
# esperado (caminho normal, slot achado por varredura):
#   crtfix: 1 slot(s) por varredura (alvo 0x6ffffe9317a0)
#   crtfix: patch slot 0x1425e1dd0 (varredura)
#   crtfix: page now RW (was 0x2)
#   crtfix: ENTER opts=0x2 buf=... n=0xffffffffffffffff fmt=[[%hhu:%hhu:%hhu]%lln]
# caminho alternativo (fallback por RVA):
#   crtfix: export=0x6ffffe931700, slot por RVA 0x25e1dd0 = 0x1425e1dd0 val=0x6ffffe9317a0
```

Atenção: o GS guarda um **ponteiro interior** (`__stdio_common_vsprintf_s +
0xA0`); se você for comparar endereços no log, o `alvo` **não** vai ser igual ao
`export`.

Se aparecer `veh: AV escrita em ... -> RW`, o handler salvou uma escrita do CRT
numa página RO — o app seguiu normalmente. Se o mesmo endereço aparecer em
loop, é bug: reporte com o log.

Para desligar o VEH e ver se o crash "original" volta (diagnóstico):
`RSPROXY_VEH=0 archicad29`.

Se nunca aparece `ENTER`, o shim foi instalado mas o formato não foi usado
(ok, significa que o crash não seria disparado). Se aparecer
`crtfix: VirtualProtect(buf=...) failed`, o hook não está protegendo o destino —
**isso** é bug: reporte com o log.

### 4. Integridade dos binários

```bash
# o patch é de 11 bytes; nada mais deve diferir
cmp -l "$AC/RS.dll.orig" "$AC/RS.dll" | wc -l        # 11
cmp -l "$AC/GSRoot.dll.orig" "$AC/GSRoot.dll" | wc -l # 11
```

### 5. Ícones

```bash
x86_64-w64-mingw32-gcc -O1 -municode -o /tmp/iconprobe.exe tools/iconprobe.c -lgdi32 -luser32
WINEPREFIX=$WINEPREFIX wine /tmp/iconprobe.exe "C:\\Program Files\\Graphisoft\\Archicad 29\\ACUserInterface.dll"
# esperado: loadfail=0
```

Ver [ICONS.md](ICONS.md) para o estado atual.

---

### 6. Ler um crash dump (`.dmp`)

Quando o crash mata o reportador, **não** sai `.RPT` — só o minidump em
`drive_c/users/<user>/Graphisoft/BugReporting-29/`. O `tools/dmpstack.py` (sem
dependências) mostra a exceção e a pilha provável:

```bash
B="$WINEPREFIX/drive_c/users/$USER/Graphisoft/BugReporting-29"
ls -t "$B"/*.dmp | head
tools/dmpstack.py "$B/CrashDump-....dmp"          # exceção + pilha da thread
tools/dmpstack.py --modules "$B"/CrashDump-....dmp  # só os módulos carregados
tools/dmpstack.py --all "$B"/CrashDump-....dmp      # todas as threads
```

A pilha é heurística: o script varre a memória da pilha atrás de valores que
caiam dentro de módulos e imprime `modulo + offset` em ordem de endereço
(crescente = ordem de chamada). Não substitui um unwinder de verdade, mas
localiza o módulo culpado — foi assim que o Bug 5.1 apareceu.

---

## Medições úteis (para comparar antes/depois)

```bash
# versão do wine e do prefixo
wine --version; cat "$WINEPREFIX/.update-timestamp"

# GPU / driver
glxinfo -B | grep -E 'device|OpenGL renderer|OpenGL version'

# uso de CPU do Archicad — NÃO use pgrep -x: a thread principal é renomeada
# para CrBrowserMain. Pegue o PID pelo cmdline:
PID=$(for d in /proc/[0-9]*; do tr '\0' ' ' < "$d/cmdline" 2>/dev/null | grep -q '^Archicad.exe' && echo "${d#/proc/}"; done | head -1)
ps -o pid,etime,pcpu,nlwp -p "$PID"
top -b -H -n 1 -p "$PID" | head -10         # por thread

# janelas reais (nomes que o Wine reporta)
tools/acstate.sh

# tamanho do log do proxy
stat -c '%s bytes' /tmp/rsproxy.log   # sem VERBOSE fica em ~2 KB
```

---

## Coleta de diagnóstico para issue

Cole no template de issue:

```bash
{
  echo "## ambiente"
  uname -a
  cat /etc/os-release | head -3
  wine --version
  glxinfo -B 2>/dev/null | grep -E 'renderer|version'
  echo "## prefixo"
  ls -l "$WINEPREFIX/drive_c/Program Files/Graphisoft/Archicad 29"/{rsproxy.dll,RS.dll.orig,GSRoot.dll.orig} 2>&1
  echo "## imports"
  llvm-readobj --coff-imports "$AC/RS.dll" | grep 'Name:' | head -3
  echo "## registro"
  wine reg query 'HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Bug reporting' 2>&1
  echo "## proxy"
  head -40 /tmp/rsproxy.log 2>/dev/null
} > diag.txt
```

Anexe também o `.RPT` mais recente (se houver) e **nunca** o projeto do cliente.
