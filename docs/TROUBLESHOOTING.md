# Solução de problemas

Sintoma → causa provável → o que fazer. Se não resolver, abra uma issue com os
dados pedidos em [TESTING.md](TESTING.md#coleta-de-diagnóstico-para-issue).

---

## O Archicad não abre nada (processo morre em segundos)

```bash
export WINEPREFIX=~/.local/share/wineprefixes/archicad29
export WINEDEBUG=+loaddll          # temporário, para ver o que falta
archicad29
```

| Cheiro no log | Causa | Correção |
|---|---|---|
| `rsproxy.dll not found` | import redirect aplicado mas proxy ausente | `cp dist/rsproxy.dll "$AC/"` |
| `err:module:import_dll Library MSVCP140.dll not found` | sem runtime VC++ | `winetricks vcrun2022` |
| `Unhandled page fault` em `Archicad.exe+0x25...` | outra build → slot de CRT diferente | ver [ARCHITECTURE §4](ARCHITECTURE.md#4-limites-conhecidos-do-design) |
| morre sem log nenhum | supervisor de bug report | conferir `disableOOPReporting=1` |

---

## Interface abre, mas sem texto

Causa: fallback MUI não está ativo. Verifique nesta ordem:

```bash
# 1. o import foi redirecionado?
llvm-readobj --coff-imports "$AC/RS.dll" | grep -c 'Name: rsproxy.dll'   # 1
llvm-readobj --coff-imports "$AC/GSRoot.dll" | grep -c 'Name: rsproxy.dll' # 1

# 2. o proxy está lá?
ls -l "$AC/rsproxy.dll"

# 3. o fallback está achando os .mui?
rm -f /tmp/rsproxy.log
RSPROXY_VERBOSE=1 archicad29     # depois de abrir, olhe:
grep -c '^SAT' /tmp/rsproxy.log  # deve ser > 0
grep '^MISS' /tmp/rsproxy.log | head
```

Se `SAT` for 0 e `MISS` for alto: os `.mui` não estão onde o proxy procura. O GS
usa `en\`, e o proxy também tenta `en-US\` e `pt-BR\`. Confira:

```bash
ls "$AC/en" | head     # deve ter ACUserInterface.dll.mui, GSRoot.dll.mui, ...
```

Se o seu Archicad estiver em outro idioma (instalador `DEU`/`FRA`), recompile com
o código do idioma em `g_langs` ([ARCHITECTURE §2](ARCHITECTURE.md#2-fallback-de-recursos-mui)).

---

## "Something went wrong" / relatório de erro do GS

O GS grava um `.RPT` para exceções **tratadas** também. Na prática, o que aparece
mais é a assert de UTF-8 do Collada (`0x0E727074`) vinda de um add-on.

```bash
B="$WINEPREFIX/drive_c/users/$USER/Graphisoft/BugReporting-29"
ls -lt "$B" | head
grep -aE 'Exception code|Fault address|Exception \(' "$B"/*.RPT | tail -20
```

- `Exception (maybe handled)` + o app continua → **não é fatal**, pode ignorar.
- `FATAL` → o app vai cair; guarde o `.RPT` e o `.dmp` para a issue.

Limpar (não apaga projeto, só relatórios):

```bash
rm -f "$B"/*.RPT "$B"/*.RPTA "$B"/*.dmp
```

---

## O app desaparece depois de alguns minutos

1. Confirme que o supervisor está desligado:
   ```bash
   wine reg query 'HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Bug reporting'
   ```
2. Veja se o Learning Center (CEF) está aberto e feche **pela janela** (não mate
   `GSCEF` por padrão de linha de comando: o padrão também casa com o próprio
   `ps`/`grep` e com os subprocessos legítimos do app).
3. Se houver `.RPT` novo, veja a seção acima — se for `combase.dll` ← `DG.dll`,
   é o bug 5 do [HISTORY](HISTORY.md#bug-5--combasedll130526--dgdll), conhecido e
   tratado.

---

## CPU alta (60–80%) com o editor parado

Conhecido (bug 6). Mitigações:

```bash
# fecha o Learning Center pela UI; se não abrir mais, desative-o:
wine reg add 'HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Work Environment' \
     /v ShowLearningCenter /t REG_DWORD /d 0 /f 2>/dev/null
```

Se a CPU cair com o Learning Center fechado, atualize a issue de CPU com essa
informação — é o dado que falta para fechar o diagnóstico.

---

## "About" fica 2 minutos / projeto demora 4 minutos

**Normal** na primeira carga (licença, add-ons, bibliotecas, primeira
compilação de GDL). Não mate o processo antes de ~5 min.

```bash
tools/acstate.sh          # mostra PID, janelas e títulos reais
tail -5 "/.../FastLogger/Log-*.log"   # "End of Shutdown" = saiu limpo
```

---

## Travou tudo / quero reiniciar sem perder o projeto

```bash
# lista só o que é Archicad (evita casar com o próprio comando):
pgrep -a -x Archicad.exe
# mata pelo PID, nunca por padrão de linha de comando:
kill -TERM <pid>
```

O Archicad faz autosave em
`drive_c/users/<user>/Graphisoft/BugReporting-29/` **não**; o autosave real fica
em `AppData\Local\Graphisoft\ 29.0.0 INT R1\AutoSave`.

---

## Problemas de GPU / tela preta no 3D

```bash
glxinfo -B | head -8          # renderer deve ser a sua GPU, não llvmpipe
```

- AMD: `export HSA_OVERRIDE_GFX_VERSION=10.3.0` (algumas RDNA2)
- NVIDIA proprietário: garantir `__GLX_VENDOR_LIBRARY_NAME=nvidia`
- Wayland: se o 3D falhar, teste `WINEPREFIX=... wine` dentro de uma sessão X11
  (ou `GDK_BACKEND=x11`), porque o XWayland muda o caminho de DRI3.

---

## "Não consigo ativar a licença"

A ativação usa rede. Se falhar com erro de rede:

```bash
wine reg add 'HKCU\Software\Wine\WineDbg' /v ShowCrashDialog /t REG_DWORD /d 0 /f
# teste a rede dentro do prefixo:
wine cmd /c "ping -n 1 graphisoft.com"
```

Se o `cmd` não resolve DNS, o problema é `winsock`/`resolv.conf` do prefixo —
normalmente resolvido com `wineboot -u` e reinício do prefixo.

---

## Coisas que **parecem** bug mas não são

| Aparência | Realidade |
|---|---|
| log do GS com timestamp vazio | esperado (shim do `%n`) |
| `.RPT` "maybe handled" | exceção tratada do add-on Collada |
| `SATLOADFAIL ... err=2` | `.mui` daquele idioma não existe — normal |
| ícones de ferramentas desabilitadas em cinza | estado da ferramenta, não bug |
| primeira abertura de projeto lenta | compilação de bibliotecas GDL |
| paleta Toolbox sem ícones em janela estreita | modo "só texto" da UI |
