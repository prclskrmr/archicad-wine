# Archicad 29 no Linux (Wine)

Rodar o **Archicad 29** (Graphisoft, Windows) em Linux via Wine, com as
correções necessárias para que a interface carregue em português/inglês, os
projetos abram, salvem e o programa feche limpo.

> **Status (22/10/2026):** funcional com ressalvas. Abre projetos AC28 e AC29,
> renderiza a UI, salva `.pln`/`.bpn` e fecha com `End of Shutdown`.
> Ver [ROADMAP.md](ROADMAP.md) para o que ainda não está resolvido.

Testado em: **Arch Linux (Omarchy)** · Wine **11.20** (staging) · AMD RX 6700 XT
(ROCm) · prefixo win64.

---

## O que funciona

| Recurso | Status |
|---|---|
| Abrir / salvar / fechar projetos (`.pln`, `.bpn` AC28 e AC29) | ✅ |
| Interface completa (menus, paletas, diálogos, textos) | ✅ |
| Render 3D / OpenGL (RADV) | ✅ |
| Ferramentas de desenho e edição | ✅ |
| Paleta Toolbox | ⚠️ alguns ícones não aparecem ([docs/ICONS.md](docs/ICONS.md)) |
| Learning Center (CEF embutido) | ⚠️ abre e consome CPU; pode fechar |
| Timestamps no log interno | ⚠️ vazios (cosmético, ver `%n`) |
| Uso prolongado (> ~10 min) | ⚠️ pode aparecer "Something went wrong" (exceção tratada) |

---

## Instalação rápida (qualquer distro)

Pré-requisitos: o **instalador oficial do Archicad 29** (`.exe`, licença sua),
`wine` ≥ 9, `winetricks` e `python3`.

```bash
git clone https://github.com/prclskrmr/archicad-wine.git
cd archicad-wine

# 1) prepara o prefixo e instala o Archicad (o instalador abre em modo gráfico)
./scripts/install.sh --installer ~/Downloads/Archicad-29-INT.exe

# 2) abre o Archicad
archicad29
```

Se o Archicad **já está instalado** no prefixo, basta aplicar as correções:

```bash
./scripts/install.sh
```

Detalhes, opções e instalação por distro: **[docs/INSTALL.md](docs/INSTALL.md)**.

---

## Por que este repositório existe

O Archicad 29 quase roda em Wine, mas trava em quatro pontos — todos
diagnosticados e contornados aqui, sem tocar no instalador original:

1. **Textos/UI vazios** — Wine não tem fallback de *MUI satellites*
   (`en/*.mui`, `pt-BR/*.mui`). Onde o Windows procuraria o recurso no `.mui`
   vizinho, o Wine devolve `NULL`.
2. **Alerta "incoherent shared library"** — o proxy de recursos muda o
   versionamento das DLLs e o GS reclama.
3. **Bug reporter fora de processo** (`disableOOPReporting`) — o supervisor
   separado do GS mata a aplicação quando o layout de janelas o confunde.
4. **Crash fatal `ucrtbase+0x16234`** — o GS formata o timestamp do log com
   `"[%hhu:%hhu:%hhu]%lln"` e `bufcnt = (size_t)-1`. O `ucrtbase` (nativo ou
   builtin) escreve fora do buffer, e o destino é uma página **somente leitura**
   dentro de `GSRoot.dll` (proteção Wibu).

A solução é um único `rsproxy.dll` (código aberto, neste repositório) que é
injetado como dependência de `RS.dll` e `GSRoot.dll` e faz três coisas:
fallback de recursos MUI, `VirtualProtect` na página de destino e shim do
`__stdio_common_vsprintf_s`.

A história completa, bug por bug, está em **[docs/HISTORY.md](docs/HISTORY.md)**;
a lista exata de arquivos alterados em **[docs/PATCHES.md](docs/PATCHES.md)**.

---

## Como funciona (resumo)

```
Archicad.exe
 ├── RS.dll        ──import──> rsproxy.dll ──forward──> KERNEL32.*
 │      └─ FindResourceW/Ex  → se o recurso não existe no módulo,
 │                              procura em <mod>.mui / en\<mod>.mui
 ├── GSRoot.dll    ──import──> rsproxy.dll (mesma coisa)
 └── thread própria do rsproxy:
        1. espera Archicad.exe+0x25E1DD0 apontar para ucrtbase
        2. VirtualProtect da página (Wibu deixa RO) + troca do ponteiro
        3. shim formata em buffer de 4096 bytes e copia para o destino
```

O redirecionamento é **um patch de 11 bytes** no nome da DLL importada dentro
do descritor de import (`KERNEL32.dll` → `rsproxy.dll`, mesmo tamanho). Os
originais ficam como `*.orig`; `./scripts/patch-mui.py --restore` desfaz.

Arquitetura detalhada: **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)**.

---

## Documentação

| Documento | Conteúdo |
|---|---|
| [docs/INSTALL.md](docs/INSTALL.md) | Instalação passo a passo, por distro, versão de Wine |
| [docs/HISTORY.md](docs/HISTORY.md) | Passos desde o início + bugs encontrados |
| [docs/PATCHES.md](docs/PATCHES.md) | Tudo que foi alterado (DLLs, registro, arquivos) |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Como o `rsproxy` funciona por dentro |
| [docs/ICONS.md](docs/ICONS.md) | Investigação dos ícones da Toolbox |
| [docs/TESTING.md](docs/TESTING.md) | Como validar que a instalação está boa |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Sintomas → causa → correção |
| [ROADMAP.md](ROADMAP.md) | Próximos aprimoramentos (com notas técnicas) |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Como contribuir |

---

## Contribuir

O objetivo é que outras pessoas possam instalar e melhorar isso. Se você tem
outra distro, outra GPU, outra versão do Archicad ou sabe mexer em Wine
(inclusive patch de `wine`/`wine-staging`), abra uma issue com o template —
os dados de diagnóstico que pedimos já vêm prontos em `docs/TESTING.md`.

Áreas onde ajuda é mais bem-vinda agora:

- **Ícones da Toolbox** (ver [docs/ICONS.md](docs/ICONS.md) — o diagnóstico já
  está feito, falta achar o caminho de renderização).
- **CPU alta** na thread principal com o editor aberto.
- **Empacotamento**: AUR (já esboçado), `.deb`, Flatpak, Nix.
- **Testes** em NVIDIA/Intel e em Wayland puro vs XWayland.

---

## Aviso legal

Este projeto **não** distribui o Archicad nem qualquer arquivo da Graphisoft.
Você precisa da sua própria licença e do instalador oficial. O que está aqui é
apenas código de compatibilidade (MIT) e documentação do processo.
