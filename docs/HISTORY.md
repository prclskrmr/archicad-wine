# Histórico: do zero até "abre, salva e fecha"

Registro cronológico do que deu errado, em que ordem, e como cada coisa foi
diagnosticada. Serve para quem for repetir o processo entender **por que** cada
correção existe (e para não desfazer uma correção achando que é ruído).

Data de referência: **22/10/2026**. Ambiente: Arch Linux (Omarchy), Wine 11.20,
prefixo win64 em `~/.local/share/wineprefixes/archicad29`, GPU AMD RX 6700 XT.

---

## Fase 0 — Instalação

1. `winetricks vcrun2022 corefonts` no prefixo limpo (win64).
2. Instalador oficial do Archicad 29 (INT) executado com `wine`. Concluiu sem
   erro; a árvore instalada tem 621 entradas em
   `drive_c/Program Files/Graphisoft/Archicad 29`.
3. Primeira execução: `Archicad.exe` sobe, mostra o splash e o "About", e
   **fica com a interface vazia** — menus e paletas sem texto.

---

## Bug 1 — Interface sem textos (MUI satellites)

**Sintoma:** janelas aparecem, mas rótulos de menu, nomes de ferramentas e
mensagens de diálogo estão vazios. O app continua vivo.

**Diagnóstico:** o Archicad 29 não guarda os textos nas DLLs principais: ele usa
*satellite DLLs* (`en\ACUserInterface.dll.mui`, `en\GSRoot.dll.mui`, …). No
Windows, quando `FindResourceW(hModule, …)` não acha o recurso no módulo, o
carregador consulta a `.mui` do idioma correspondente. **O Wine não tem esse
fallback** — devolve `NULL` e o GS fica sem string.

**Correção:** proxy de import. `RS.dll` e `GSRoot.dll` passam a importar
`rsproxy.dll` no lugar de `KERNEL32.dll`; o proxy tenta o recurso no módulo
original e, se falhar, tenta `en\<mod>.mui`, `en-US\<mod>.mui`,
`pt-BR\<mod>.mui` (nessa ordem), carregadas com `LOAD_LIBRARY_AS_DATAFILE`.
O `HRSRC` devolvido é registrado para que `SizeofResource`/`LoadResource`
sejam chamados com o módulo certo (o `.mui`, não o original).

Detalhes técnicos e offsets: [PATCHES.md](PATCHES.md#1-import-redirect-rsdll--gsrootdll).

---

## Bug 2 — "Incoherent shared library" a cada start

**Sintoma:** alerta do GS reclamando que a DLL carregada não corresponde à
assinatura/versão esperada. Aparece porque o `rsproxy.dll` entra no lugar do
`KERNEL32.dll` e o GS compara o *checksum* de imports das DLLs.

**Correção:** `disableIncoherentSharedLibraryAlert = 1` em
`HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Bug reporting`.

---

## Bug 3 — O supervisor de bug report mata o programa

**Sintoma:** o Archicad some sozinho depois de alguns minutos, sem crash dump e
sem mensagem, mesmo com a interface funcionando. Nos logs, o processo
`BugReporting-29` (separado) aparecia logo antes.

**Causa:** o GS roda um *out-of-process reporter*. Sob Wine, quando ele decide
que a janela principal "não responde" (durante carregamentos de 2–3 min), ele
mata a aplicação.

**Correção:** `disableOOPReporting = 1` na mesma chave. O relatório passa a ser
gravado no mesmo processo, em
`drive_c/users/<user>/Graphisoft/BugReporting-29/`.

---

## Bug 4 — Crash fatal em `ucrtbase+0x16234` (o problema real)

**Sintoma:** com a UI já funcional, o app morria ao abrir projeto. Relatório
`FATAL` com:

```
Exception code: C0000005 ACCESS_VIOLATION writing location: 00006FFFF9AADE88
Fault address:  ucrtbase.dll + 0x16234
Call stack:
  ucrtbase.dll + 16234
  GSRoot.dll   + ...
  Archicad.exe + 25E1DD0 (slot de CRT)
```

**Diagnóstico (passo a passo):**

1. A escrita inválida cai em `0x6FFFF9AADE88`, que fica **dentro de
   `GSRoot.dll`** — e não é uma página de pilha. `VirtualQuery` mostrou
   `PAGE_READONLY`: é uma seção protegida pelo Wibu (o empacotador do
   `Archicad.exe`), mapeada RO.
2. O `rsproxy` logou o formato exato passado ao CRT:

   ```
   crtfix: ENTER opts=0x2 buf=00006FFFF9AADE88 n=0xffffffffffffffff fmt=[[%hhu:%hhu:%hhu]%lln]
   ```

   Ou seja: `__stdio_common_vsprintf_s(opts, buf, bufcnt=(size_t)-1, fmt, …)`.
3. `%n` não é implementado no `ucrtbase` do Wine. Com `bufcnt = SIZE_MAX`, o
   `_vsnprintf` interno calcula o endereço do terminador como
   `buf + bufcnt` → escrita fora do buffer → `ACCESS_VIOLATION`.
4. Isto é usado no **timestamp do log do GS** (`[hh:mm:ss]`), que roda cedo no
   carregamento do projeto — por isso o crash sempre acontecia no mesmo ponto.

**Correção (shim de CRT dentro do `rsproxy.dll`):**

- uma thread do proxy espera o ponteiro em `Archicad.exe + 0x25E1DD0` apontar
  para `ucrtbase` e o substitui pelo shim (`InterlockedExchangePointer`);
- a **página do slot** é RO (Wibu) → `VirtualProtect` antes de escrever;
- o shim faz `VirtualProtect(buf, 0x1000, PAGE_READWRITE)` na **página de
  destino** (é isso que impede o AV de escrita);
- formata em buffer próprio de 4096 bytes com limite 4095, em vez de `SIZE_MAX`,
  e copia o resultado para o buffer do chamador.

Resultado: `[%hhu:%hhu:%hhu]%lln` produz string **vazia** (porque o `%n` falha
no `ucrtbase`), mas não corrompe mais nada. É cosmético: o timestamp do log
interno fica em branco. Tentar "consertar" reescrevendo `%n` → `%.0s`
(`RSPROXY_SANITIZE=1`) **derruba o app** — o chamador não espera texto ali.
Ficou *opt-in* e documentado como não usar.

---

## Bug 5 — `combase.dll+130526` ← `DG.dll`

**Sintoma:** depois de ~8 minutos de uso, relatório `Exception (maybe handled)`:

```
ACCESS_VIOLATION reading location: 0000000000000000
combase.dll + 130526
combase.dll + 45306
DG.dll      + 1730786   (DGTransparentWindow / OLE)
DG.dll      + 709453
Archicad.exe+ 32665260
```

**Status:** **não é fatal** — é tratado pelo VEH do GS, gera `.RPT` e o app
continua (o log termina com `End of Shutdown`). É o que faz aparecer
"Something went wrong" de vez em quando. Suspeita: caminho de OLE/drag-and-drop
da janela transparente do `DG` (`DoDragDrop`/`IDropTarget`) não implementado no
Wine. Está no [ROADMAP](../ROADMAP.md) como item aberto.

Observação: matar os processos `GSCEF` (Learning Center/CEF) **por PID** não
derruba o app, mas aumenta a chance desse caminho ser exercitado. O jeito
seguro de fechar o Learning Center é pela própria janela.

---

## Bug 6 — CPU em ~70% na thread principal

**Sintoma:** com o editor aberto, a thread principal (`CrBrowserMain`, o CEF
renomeia) fica em ~70% de CPU sem parar, mesmo em repouso.

**Diagnóstico:** não é o logging do proxy (o build silencioso tem o mesmo
comportamento). A thread é a mesma do CEF/Learning Center. Provável laço de
repaint do `DGTransparentWindow` sob Wine.

**Status:** aberto, não impede o uso. Ver [ROADMAP](../ROADMAP.md).

---

## Bug 7 — Ícones da Toolbox faltando

**Sintoma:** a paleta Toolbox mostra vários botões sem ícone.

**Diagnóstico (feito):** os ícones **não** vêm de `FindResource` (o log do proxy
não registra nenhuma consulta de tipo `ICON`/`BITMAP`), e **carregam
normalmente**: `tools/iconprobe.c` carrega os 11.231 `RT_GROUP_ICON` do
`Archicad.exe` com `LoadImageW` + `GetDIBits` → 11.207 OK, 23 "vazios" e 1 falha.
Inspecionando os 23: todos são PNG 8-bit RGBA **pretos com alfa** (silhueta para
o GS tingir), portanto visíveis; apenas `I29641` é realmente transparente.

Ou seja: o problema não é decodificação de ícone pelo Wine, é o **caminho de
renderização do DG** (Direct2D/WIC) ou a escolha de variante de DPI.
Detalhes e próximos passos: [ICONS.md](ICONS.md).

**Status:** aberto.

---

## Linha do tempo resumida

| # | Sintoma | Causa raiz | Correção |
|---|---|---|---|
| 1 | UI sem texto | Wine sem fallback MUI | `rsproxy` + patch de import |
| 2 | Alerta "incoherent shared library" | proxy muda checksum | chave de registro |
| 3 | App some sozinho | OOP reporter mata | chave de registro |
| 4 | Crash ao abrir projeto | `%lln` + `bufcnt=-1` + página RO | shim de CRT + `VirtualProtect` |
| 5 | "Something went wrong" esporádico | OLE/DG no Wine | aberto (tratado, não fatal) |
| 6 | CPU ~70% | repaint CEF/DG | aberto |
| 7 | Ícones da Toolbox | renderização DG, não decodificação | aberto |
