# Arquitetura do `rsproxy.dll`

Um único DLL de ~130 KB resolve duas classes de problema: **recursos MUI** e
**formatação de CRT**. Este documento explica o mecanismo interno, para quem
quiser depurar ou estender.

```
┌──────────────────────────────────────────────────────────────────┐
│ Archicad.exe                                                     │
│   ├─ RS.dll ─────── import KERNEL32 → rsproxy.dll                │
│   ├─ GSRoot.dll ─── import KERNEL32 → rsproxy.dll                │
│   └─ rsproxy.dll                                                 │
│        ├─ FindResourceW / FindResourceExW  (fallback .mui)       │
│        ├─ SizeofResource / LoadResource / LockResource           │
│        ├─ 173 forwarders → KERNEL32.*                            │
│        └─ thread: CRT slot hook + VirtualProtect                 │
└──────────────────────────────────────────────────────────────────┘
```

---

## 1. Injeção sem `LD_PRELOAD`

O Wine não permite `LD_PRELOAD` de DLL PE dentro de um processo Windows. A
injeção usada é a mais simples e estável: **import table redirect**.

`RS.dll` e `GSRoot.dll` importam ~180 funções de `KERNEL32.dll`. Trocando o
*nome* da DLL no descritor de import por `rsproxy.dll` (mesmo número de bytes),
o carregador do Wine resolve essas ~180 funções no nosso binário — que as
reexporta por *forwarding* para `KERNEL32`. Só 6 delas são implementadas de
fato:

| Função | Papel |
|---|---|
| `FindResourceW` | tenta o módulo original; se falhar, tenta o `.mui` |
| `FindResourceExW` | idem, ignorando o idioma pedido |
| `SizeofResource` | redireciona para o módulo onde o `HRSRC` realmente está |
| `LoadResource` | idem |
| `LockResource` | passa direto (o `HGLOBAL` já é do `.mui`) |
| `FreeResource` | passa direto |

O `.def` tem 181 linhas: 6 implementações + 173 forwarders. Gerar os
forwarders é mecânico — a lista completa está em
[`src/rsproxy/rsproxy.def`](../src/rsproxy/rsproxy.def) e foi derivada dos
imports reais dos dois DLLs (mais uma folga para funções usadas por código
carregado depois).

> **Por que não interceptar `LoadLibrary`/`GetProcAddress`?** Porque o GS usa
> import estático e o loader resolve tudo antes do `DllMain`. O redirect é a
> única forma de entrar antes da primeira chamada.

---

## 2. Fallback de recursos MUI

### O problema

`FindResourceW(hModule, name, type)` no Windows, quando o recurso não existe no
módulo, consulta a *language resource file* (`<mod>.dll.mui` ou `<mod>.mui` em
`<lang>\`). O Wine implementa `LoadString`/`LoadLibrary` com `.mui` em alguns
casos, mas **não** o `FindResource` genérico. Como o GS usa `FindResource`
direto para tudo (strings, tabelas, cursores, JSSC…), o resultado é UI vazia.

### A implementação

```
FindResourceW(hModule, lpName, lpType)
  ├─ rFindResourceW(hModule, …)   → achou? devolve (caminho normal, 99% dos casos)
  └─ não achou:
       path = GetModuleFileNameW(hModule)
       para lang em { "en", "en-US", "pt-BR" }:
           cand1 = <dir>\<lang>\<base>.mui     ex.: en\ACUserInterface.dll.mui
           cand2 = <dir>\<lang>\<stem>.mui     ex.: en\ACUserInterface.mui
           sat = LoadLibraryExW(cand, LOAD_LIBRARY_AS_DATAFILE)   [cacheado]
           h = rFindResourceW(sat, lpName, lpType)
           se h: registra (h → sat, type, name) e devolve h
       devolve NULL
```

Detalhes que importam:

- **Cache de módulos** (`MAXCACHE = 256`, comparado com `_wcsicmp`) porque o GS
  consulta milhares de recursos por segundo; abrir o `.mui` toda vez mataria a
  performance.
- **Mapa `HRSRC → HMODULE`** (`g_map`, cresce em potência de 2): o `HRSRC`
  devolvido pertence ao `.mui`, mas o GS chama `SizeofResource(hModule_original,
  h)` depois. Sem o mapa, o Wine devolveria 0/tamanho errado. O mapa também
  guarda tipo/nome, usados no log.
- **`LOAD_LIBRARY_AS_DATAFILE`**: carrega o `.mui` sem executar `DllMain` nem
  resolver imports (é só um contêiner de recursos).
- **Ordem dos idiomas** é fixa (`en`, `en-US`, `pt-BR`). Para outras línguas,
  edite `g_langs` em [`rsproxy.c`](../src/rsproxy/rsproxy.c) e recompile — ver
  [ROADMAP](../ROADMAP.md).
- **Thread-safety**: tudo que toca `g_map`/`g_cache` usa `CRITICAL_SECTION`
  (o GS consulta recursos de várias threads).

### Log

```
SAT       mod=…\RS.dll type=STRS name=1234        recurso veio do .mui
MISS      mod=…\RS.dll type=STRS name=9999        não existe em lugar nenhum
RESLOAD   type=FILE name=… size=… first=89504e47  conteúdo do recurso
SATLOADFAIL path=…\pt-BR\x.mui exists=0 err=2     .mui não existe (normal)
```

Sem `RSPROXY_VERBOSE=1`, só as 20 primeiras linhas saem — o GS consulta strings
a cada repaint e o log passa de 50 MB em minutos.

---

## 3. Shim de CRT (`__stdio_common_vsprintf_s`)

O `Archicad.exe` guarda em um slot global (`RVA 0x25E1DD0`) o ponteiro para a
função de formatação do `ucrtbase`. O GS formata o timestamp do log assim:

```c
__stdio_common_vsprintf_s(0x2, buf, (size_t)-1, "[%hhu:%hhu:%hhu]%lln", NULL, ap);
```

Dois problemas simultâneos:

1. `%n` não existe no `ucrtbase` do Wine → o formatador faz o "write-back" de
   contagem usando `buf + bufcnt`, com `bufcnt = SIZE_MAX`.
2. `buf` aponta para dentro de `GSRoot.dll`, em página **`PAGE_READONLY`**
   (seção protegida pelo Wibu) — então até uma escrita "correta" ali falharia.

### A sequência

```
thread própria (criada no DllMain)
  loop a cada 50 ms:
    exe = GetModuleHandleW(NULL)
    se nome != "Archicad.exe": continua
    slot = exe + 0x25E1DD0
    v = *slot
    se v não está em [ucrtbase, ucrtbase+0x400000): continua
    VirtualProtect(slot, 8, PAGE_READWRITE)      // página do slot também é RO
    InterlockedExchangePointer(slot, &crt_vsprintf_s)
```

```
crt_vsprintf_s(opts, buf, n, fmt, loc, ap)
  VirtualProtect(buf, 0x1000, PAGE_READWRITE)     // página de destino
  se n > 0x1000000 (bufcnt absurdo):
      formata em tmp[4096] com n = 4095
      copia r+1 bytes para buf
  senão: chama o CRT original (comportamento normal preservado)
```

Por que `0x1000` e não o tamanho exato: `VirtualProtect` trabalha por página e o
buffer fica no meio de uma. Uma única chamada basta e é memorizada
(`g_buf_rw`), então o custo é pago uma vez.

### Efeitos colaterais conhecidos

- O timestamp do log do GS sai **vazio** (`r = 0`), porque o `%n` falha e o
  `ucrtbase` devolve `-1`. Cosmético.
- `RSPROXY_SANITIZE=1` reescreve `%n` → `%.0s` para produzir o texto, mas o
  chamador do GS não espera string ali e o app cai. Ficou *opt-in* apenas para
  experimentos.
- O desvio do slot é por *polling* (50 ms). Se o GS reinicializar o CRT depois
  (não acontece na prática), o hook se reaplica sozinho.

---

## 4. Limites conhecidos do design

| Limite | Consequência | Solução futura |
|---|---|---|
| Só `RS.dll` e `GSRoot.dll` importam o proxy | Um terceiro módulo que carregue recursos não terá fallback | aplicar o patch em mais DLLs (`patch-mui.py` já é genérico) |
| Lista de idiomas fixa | `de`, `fr`, `it`… não funcionam | ler de `LANG`/registro |
| `RVA 0x25E1DD0` fixo | quebra em outra build do `Archicad.exe` | localizar o slot por assinatura (ver ROADMAP) |
| Sem hook de `LoadImage`/`LoadIcon` | ícones de `.mui` (não usados hoje) não teriam fallback | estender o proxy |
| Log em `/tmp/rsproxy.log` | sem rotação | usar `RSPROXY_LOG` + limite |

---

## 5. Recompilar

```bash
cd src/rsproxy
make                 # x86_64-w64-mingw32-gcc -O2 -shared -o rsproxy.dll rsproxy.c rsproxy.def
make check           # confere se o .def exporta tudo que RS.dll/GSRoot.dll importam
```

Depois copie para `dist/rsproxy.dll` e/ou rode `scripts/install.sh` de novo (o
launcher detecta a diferença e atualiza o prefixo mantendo `.prev`).

Para inspecionar o que o binário exporta:

```bash
x86_64-w64-mingw32-objdump -p rsproxy.dll | sed -n '/Export Address Table/,/^$/p' | head -20
```
