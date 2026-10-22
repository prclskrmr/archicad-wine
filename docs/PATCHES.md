# O que é alterado (e onde)

Inventário completo de tudo que este projeto modifica. Nada aqui toca o
instalador da Graphisoft: as mudanças são aplicadas **depois** da instalação,
no prefixo Wine, e todas são reversíveis.

Convenções:

```bash
WINEPREFIX=${WINEPREFIX:-$HOME/.local/share/wineprefixes/archicad29}
AC="$WINEPREFIX/drive_c/Program Files/Graphisoft/Archicad 29"
```

---

## 1. Import redirect: `RS.dll` e `GSRoot.dll`

### O que muda

O **nome da DLL** dentro do descritor de import que aponta para
`KERNEL32.dll` é sobrescrito in-place por `rsproxy.dll`.

```
antes:  4B 45 52 4E 45 4C 33 32 2E 64 6C 6C 00   "KERNEL32.dll\0"  (13 bytes)
depois: 72 73 70 72 6F 78 79 2E 64 6C 6C 00 00   "rsproxy.dll\0\0" (13 bytes)
```

Mesmo comprimento → **nenhum offset do PE muda**, nenhuma seção nova, nenhum
checksum de cabeçalho a recalcular. O carregador resolve as funções de
`KERNEL32` pelo `rsproxy.dll`, que as reexporta como *forwarders*
(`AcquireSRWLockExclusive=KERNEL32.AcquireSRWLockExclusive`, etc.).

### Onde exatamente

Na instalação de referência (Archicad 29.0.0 INT R1, build 3000):

| Arquivo | Descritor de import | Offset do arquivo (0-based) | Bytes alterados |
|---|---|---|---|
| `RS.dll` | 0 (o primeiro, `KERNEL32.dll`) | `0x19E4E` (106.062) | 11 |
| `GSRoot.dll` | 1 (o segundo; o 0 é `ADVAPI32.dll`) | `0x17C3EC` (1.557.228) | 11 |

> Esses offsets **variam** entre builds/idiomas. Use o script — ele localiza o
> descritor pelo nome, não pelo offset:

```bash
python3 scripts/patch-mui.py "$AC/RS.dll" "$AC/GSRoot.dll"
# criado backup: RS.dll.orig, GSRoot.dll.orig
python3 scripts/patch-mui.py --restore "$AC/RS.dll" "$AC/GSRoot.dll"
```

Verificação (deve mostrar `rsproxy.dll` na lista de imports):

```bash
llvm-readobj --coff-imports "$AC/RS.dll" | grep 'Name:'
#  Name: rsproxy.dll
#  Name: MSVCP140.dll
#  ...
```

### Por que esses dois arquivos

São os dois módulos que o GS usa para carregar recursos de UI/strings.
`RS.dll` = "Resource System"; `GSRoot.dll` = runtime base. O `Archicad.exe`
não é tocado (é empacotado por Wibu; alterar o arquivo invalida a assinatura).

---

## 2. `rsproxy.dll` (novo arquivo)

```
$AC/rsproxy.dll          # binário (dist/rsproxy.dll ou compilado)
$AC/rsproxy.dll.prev     # versão anterior, criada pelo launcher a cada update
```

O launcher mantém uma cópia de referência em
`~/.local/share/archicad-wine/rsproxy.dll`; se ela for diferente da que está no
prefixo, o launcher copia (guardando a antiga em `.prev`). Assim atualizar o
repositório atualiza a instalação, sem reinstalar nada.

Comportamento conforme variáveis de ambiente:

| Variável | Efeito |
|---|---|
| *(nenhuma)* | logging de recursos só nas ~20 primeiras linhas |
| `RSPROXY_VERBOSE=1` | loga toda consulta (`SAT`, `MISS`, `RESLOAD`, `SATLOADFAIL`) em `/tmp/rsproxy.log` |
| `RSPROXY_SANITIZE=1` | reescreve `%n` → `%.0s` — **derruba o app**, não use (ver [HISTORY](HISTORY.md#bug-4--crash-fatal-em-ucrtbase0x16234-o-problema-real)) |

Fonte: [`src/rsproxy/rsproxy.c`](../src/rsproxy/rsproxy.c) ·
build: [`src/rsproxy/Makefile`](../src/rsproxy/Makefile).

---

## 3. Registro (HKCU)

Tudo em `HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1`:

### `\Bug reporting`

| Valor | Tipo | Valor | Motivo |
|---|---|---|---|
| `disableIncoherentSharedLibraryAlert` | DWORD | `1` | silencia o alerta causado pelo import redirect |
| `disableOOPReporting` | DWORD | `1` | impede o supervisor externo de matar o app |

```bash
wine reg add 'HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Bug reporting' \
     /v disableIncoherentSharedLibraryAlert /t REG_DWORD /d 1 /f
wine reg add 'HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Bug reporting' \
     /v disableOOPReporting /t REG_DWORD /d 1 /f
```

### `\Special Folder Locations`

O GS não cria sozinho a árvore de pastas de usuário no Wine e fica reclamando
de "Data Folder"/"AutoSave Folder". O launcher/instalador grava:

```
[Software\GRAPHISOFT\ 29.0.0 INT R1\Special Folder Locations]
"User Folder"="C:\users\<user>\Documents\Graphisoft\ 29.0.0 INT R1"
"Cache Folder"="C:\users\<user>\AppData\Local\Graphisoft\ 29.0.0 INT R1\Cache"
"Log Folder"="C:\users\<user>\AppData\Local\Graphisoft\ 29.0.0 INT R1\Logs"
"Temporary Folder"="C:\users\<user>\AppData\Local\Temp"
"Data Folder"="C:\users\<user>\AppData\Local\Graphisoft\ 29.0.0 INT R1"
"AutoSave Folder"="C:\users\<user>\AppData\Local\Graphisoft\ 29.0.0 INT R1\AutoSave"
"Web Objects Folder"="C:\users\<user>\AppData\Local\Graphisoft\ 29.0.0 INT R1\WebObjects"
"Templates Folder"="C:\users\<user>\AppData\Local\Graphisoft\ 29.0.0 INT R1\Templates"
```

(observe o **espaço** antes de `29.0.0` — é assim que o GS monta o nome da
chave; não "corrija")

---

## 4. Arquivos no Linux (fora do prefixo)

| Caminho | O que é |
|---|---|
| `~/.local/share/wineprefixes/archicad29/` | prefixo Wine (pode mover com `--prefix`) |
| `~/.local/share/archicad-wine/rsproxy.dll` | cópia de referência do payload |
| `~/.local/share/archicad-wine/rsproxy.dll.works-backup` | último build validado (rollback) |
| `~/.local/bin/archicad29` | launcher instalado pelo `install.sh` |
| `/tmp/rsproxy.log` | log do proxy (só com `RSPROXY_VERBOSE=1` cresce muito) |

Nada é escrito em `/usr` e nada em `/usr/share/omarchy` (arquivos de pacote são
sobrescritos em updates do sistema).

---

## 5. O que **não** é alterado

- Instalador, `Archicad.exe`, licenciamento/Wibu.
- `RS.dll`/`GSRoot.dll` na versão original (guardadas como `.orig`).
- Registro global do Wine (`system.reg`) — só o `user.reg` do prefixo.
- Bibliotecas do sistema (não há patch de `wine` nem de `dxvk`).

---

## 6. Rollback completo

```bash
# 1. desfaz o import redirect
python3 scripts/patch-mui.py --restore "$AC/RS.dll" "$AC/GSRoot.dll"

# 2. remove o proxy
rm -f "$AC/rsproxy.dll" "$AC/rsproxy.dll.prev"

# 3. (opcional) remove as chaves de registro
wine reg delete 'HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Bug reporting' /f
```

Sem os passos 1–2 o Archicad **não abre** (o import de `KERNEL32` fica apontando
para um arquivo que não existe).
