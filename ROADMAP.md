# Roadmap — próximos aprimoramentos

Lista priorizada do que falta. Cada item tem o **estado do diagnóstico** (para
ninguém recomeçar do zero) e uma sugestão de como atacar.

Legenda: 🔴 bloqueia uso sério · 🟡 incomoda · 🟢 polimento

---

## 🔴 1. Ícones da Toolbox

**Estado:** diagnóstico feito, causas erradas eliminadas.
`LoadImage` decodifica 11.207/11.231 ícones do `Archicad.exe`; os 23 "vazios"
são silhuetas pretas com alfa legítimas; os `.mui` não têm ícones; todas as bases
`I<id>` têm variante `&96`. A chave interna `PNG Icon Loading Patch` (0 → 1) foi
testada e **não** resolve por completo. Ver [docs/ICONS.md](docs/ICONS.md).

**Hipótese principal:** caminho Direct2D/WIC do `DG` (o `D2DWRenderManager` é
usado para desenhar a UI) devolvendo bitmap vazio para certos PNGs.

**Próximos passos:**

1. Hook no `rsproxy` para `LoadImageW`, `LoadIconW`, `LoadBitmapW`,
   `CreateIconFromResourceEx` logando `(name, cx, cy, retorno)` — descobre se o
   GS **pede** o ícone e o que recebe.
2. Testar `WINEDLLOVERRIDES="d2d1=d"` — se os ícones voltarem, o bug é `d2d1`.
3. Reproduzir a decodificação isolada (WIC / GDI+ / D2D) com um PNG extraído
   (`tools/peicons.py --dump`) e reportar upstream com o arquivo em anexo.
4. Identificar os IDs `I<id>` das ferramentas afetadas (lista na issue) para
   cruzar com o código do GS.

---

## 🔴 2. Crash `combase.dll+130526` ← `DG.dll` (OLE / drag-and-drop)

**Estado:** exceção **tratada** pelo VEH (o app continua e sai com
`End of Shutdown`), mas gera `.RPT` e o diálogo "Something went wrong".
Stack:

```
combase.dll + 130526   (leitura de NULL)
combase.dll + 45306    (OleDoDragDrop / IDataObject)
DG.dll      + 1730786  (DGTransparentWindow)
DG.dll      + 709453
Archicad.exe+ 32665260
```

**Hipóteses:** `IDropTarget`/`IDataObject` incompletos no Wine; janela
transparente do DG registrando drop target em HWND errado.

**Como atacar:** `WINEDEBUG=+ole,+dragdrop` por 2 min reproduz o caminho;
comparar com o teste oficial de drag-and-drop do Wine
(`dlls/ole32/tests/` e `dlls/shell32/tests/`). Se for bug do Wine, reportar
com o `.RPT` (o endereço exato está nele).

---

## 🟡 3. CPU ~70% na thread principal (CEF / Learning Center)

**Estado:** não é o logging do proxy (build silencioso tem o mesmo perfil). A
thread é a do CEF (`CrBrowserMain`): medido com `top -H`, a thread principal
(TID = PID) fica em **~100%**, e uma `Timer-S...` em ~10%. `perf` não está
instalado e `ptrace_scope=1` bloqueia `gdb`/`eu-stack` em processo já rodando —
para amostrar a pilha é preciso ou instalar `perf` (`pacman -S perf`) ou subir o
app como filho do `gdb`. Suspeita: laço de repaint do `DGTransparentWindow` ou
do browser host.

**Como atacar:**

```bash
# 1. mapear quem queima CPU
top -H -p "$(pgrep -x Archicad.exe)"
# 2. fechar o Learning Center pela UI e comparar
# 3. desabilitar o CEF por completo (se houver opção/registro) e medir
```
Se cair para ~0% com o Learning Center fechado, o próximo passo é investigar
`GSCEF/CefSharp.BrowserSubprocess.exe` (versão do CEF vs Wine) e, no limite,
substituir o host do Learning Center por um stub.

---

## ✅ 4. Localizar o slot de CRT por assinatura (não por RVA fixo)

**Estado:** feito. A varredura percorre as seções graváveis do `Archicad.exe`
procurando qwords a até **±0x400** das exportações `__stdio_common_vsprintf*`
do `ucrtbase` — necessário porque o GS guarda um **ponteiro interior**
(`__stdio_common_vsprintf_s + 0xA0`), não o endereço exportado. Se não achar,
usa o RVA desta build, que agora também pode ser passado por
`RSPROXY_SLOT_RVA=<hex>` sem recompilar. Ver
[ARCHITECTURE §3](docs/ARCHITECTURE.md#3-shim-de-crt-__stdio_common_vsprintf_s).

**O que falta:** um teste que rode a varredura contra uma cópia do
`Archicad.exe` **sem** precisar abrir o app (hoje a validação é pelo log na
primeira abertura).

---

## 🟡 5. Timestamps do log + `%n` de verdade

**Estado:** o shim zera o resultado (string vazia) porque o `%n` falha no
`ucrtbase`; `RSPROXY_SANITIZE=1` produz o texto mas derruba o app.

**Solução ideal:** implementar o formatador completo no shim (suportar `%n`
escrevendo no ponteiro do argumento e continuar), em vez de delegar ao
`ucrtbase`. É um `vsnprintf` próprio, ~200 linhas, e resolve os dois problemas
(timestamp correto e sem crash).

**Atalho:** fazer o shim interceptar apenas quando `bufcnt == SIZE_MAX` **e** o
formato contiver `%n`, formatando tudo por conta própria.

---

## 🟢 6. Proxy de recursos genérico (mais DLLs, mais idiomas)

- Aplicar o import redirect também em `ACUserInterface.dll`, `DG.dll`,
  `ACData.dll` (se algum dia importarem `KERNEL32` — hoje só RS/GSRoot importam
  o suficiente).
- Ler o idioma de `LANG`/registro em vez de `{"en","en-US","pt-BR"}` fixo.
- Adicionar fallback de **ícones/bitmaps** (`LoadImageW`/`LoadIconW`) para
  `.mui`, caso uma versão futura passe a guardar ícones lá.

---

## 🟢 7. Automação de instalação e testes

- `install.sh --non-interactive` de ponta a ponta (instalador do Archicad com
  resposta automática, se o instalador permitir `/S`).
- Smoke test automatizado: abrir um projeto fixture, esperar a janela, salvar em
  outro arquivo, comparar tamanho/hash e conferir `End of Shutdown` no log.
- CI (GitHub Actions) que compila o `rsproxy` com `mingw-w64` e roda `shellcheck`
  + `python -m py_compile` nos scripts. (Já esboçado em
  `.github/workflows/build-rsproxy.yml`.)
- Checksums SHA-256 dos binários em `dist/` e verificação no `install.sh`.

---

## 🟢 8. Empacotamento

| Formato | Estado |
|---|---|
| Script portátil (`scripts/install.sh`) | ✅ |
| PKGBUILD / AUR | 🟡 esboço em `packaging/PKGBUILD` |
| `.deb` (Debian/Ubuntu) | ⬜ |
| Flatpak (com Wine embutido) | ⬜ discutir viabilidade (licença/instalador) |
| Nix flake | ⬜ |

---

## 🟢 9. Compatibilidade com outras versões

- Archicad **28** e **27**: mesma arquitetura (MUI + `%n`), offsets diferentes.
  O `patch-mui.py` já é genérico; o slot de CRT precisa de tabela por versão.
- Archicad **30+**: verificar se continuam usando `KERNEL32` import em
  `RS.dll`/`GSRoot.dll` e `%n` no log.

---

## 🟢 10. Documentação viva

- [ ] Gravar um vídeo curto do processo (instalação → projeto aberto).
- [ ] Tabela de GPUs/drivers testados (NVIDIA, Intel, RADV, amdgpu-pro).
- [ ] Traduzir README para inglês (hoje só pt-BR).
- [ ] Publicar os `.RPT` anonimizados como *fixtures* de referência.

---

## Histórico de itens concluídos

| Item | Onde está documentado |
|---|---|
| Fallback MUI (RS/GSRoot) | [HISTORY §Bug 1](docs/HISTORY.md#bug-1--interface-sem-textos-mui-satellites) |
| Alerta "incoherent shared library" | [HISTORY §Bug 2](docs/HISTORY.md#bug-2--incoherent-shared-library-a-cada-start) |
| OOP reporter matando o app | [HISTORY §Bug 3](docs/HISTORY.md#bug-3--o-supervisor-de-bug-report-mata-o-programa) |
| Crash fatal `%lln` | [HISTORY §Bug 4](docs/HISTORY.md#bug-4--crash-fatal-em-ucrtbase0x16234-o-problema-real) |
| Launcher que se auto-atualiza | [PATCHES §2](docs/PATCHES.md#2-rsproxydll-novo-arquivo) |
| Slot de CRT por varredura (±0x400) + `RSPROXY_SLOT_RVA` | [ARCHITECTURE §3](docs/ARCHITECTURE.md#3-shim-de-crt-__stdio_common_vsprintf_s) |
| VEH para escrita do CRT em página RO de módulo | [HISTORY §Bug 5.1](docs/HISTORY.md#bug-51--ucrtbase0x17234-escrevendo-em-gsrootdll-o-mesmo-mal-outro-caminho) |
| `tools/dmpstack.py` (pilha a partir do minidump) | [TESTING](docs/TESTING.md#6-ler-um-crash-dump-dmp) |
