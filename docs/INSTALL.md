# Instalação

Este documento cobre a instalação do zero em várias distribuições, a versão de
Wine recomendada e o que cada etapa resolve.

Se você só quer o caminho mais curto: [README › Instalação rápida](../README.md).

---

## 1. Requisitos

### Arquivos que você precisa ter

- Instalador oficial do **Archicad 29** (`Archicad-29-...-INT.exe`), da sua conta
  Graphisoft. Este projeto não distribui nada da Graphisoft.
- Chave de licença já ativada (o Archicad 29 usa o sistema de licença local;
  a ativação é feita na primeira execução, como no Windows).

### Versão do Wine

| Versão | Situação |
|---|---|
| **11.x** | ✅ testado (11.20), é o alvo deste repositório |
| 10.x | ✅ deve funcionar (mesma arquitetura de `ucrtbase`) |
| 9.x | ⚠️ funciona, mas o `rsproxy` foi validado em 11.x |
| ≤ 8.x | ❌ não recomendado (Direct2D e `ucrtbase` antigos) |

Use **wine-staging** quando existir na sua distro: as correções de Direct2D e
de janelas que o Archicad usa entram primeiro lá. O prefixo precisa ser
**win64** (o Archicad 29 não tem mais build 32 bits).

```bash
wine --version     # precisa responder 9.0 ou maior
```

### Dependências obrigatórias instaladas **no prefixo**

O Archicad depende do runtime da Microsoft e de fontes. Sem isso ele nem chega
a desenhar a interface:

```bash
winetricks --unattended vcrun2022 corefonts
```

- `vcrun2022` → `msvcp140`, `vcruntime140`, `ucrtbase` nativos (o Archicad 29 é
  compilado com VS2022; o `ucrtbase` nativo também é o que o shim de CRT
  substitui em tempo de execução).
- `corefonts` → Arial, Times, Verdana etc. Sem isso os diálogos ficam com
  texto cortado/quadrados.

> O prefixo de referência usado no desenvolvimento foi criado exatamente assim
> (`winetricks.log` = `vcrun2022 andale arial ... corefonts`).

---

## 2. Instalação por distro

### Arch Linux / Manjaro / Omarchy

```bash
sudo pacman -S --needed wine winetricks python
# opcional: para compilar o rsproxy localmente em vez de usar dist/rsproxy.dll
sudo pacman -S --needed mingw-w64-gcc
```

Se preferir a versão staging (recomendado):

```bash
sudo pacman -S --needed wine-staging winetricks   # conflita com `wine`; escolha um
```

### Debian / Ubuntu / Linux Mint

O `wine` dos repositórios oficiais costuma ser antigo. Prefira o repositório
oficial do WineHQ:

```bash
sudo dpkg --add-architecture i386
sudo mkdir -pm755 /etc/apt/keyrings
sudo wget -O /etc/apt/keyrings/winehq-archive.key https://dl.winehq.org/wine-builds/winehq.key
sudo wget -NP /etc/apt/sources.list.d/ https://dl.winehq.org/wine-builds/debian/dists/bookworm/winehq-bookworm.sources
sudo apt update
sudo apt install --install-recommends winehq-staging winetricks python3
```

(troque `bookworm` por `trixie`, `jammy`, `noble` etc. conforme a sua versão)

### Fedora

```bash
sudo dnf install wine winetricks python3
# se o wine do Fedora estiver velho, use o COPR do WineHQ:
#   sudo dnf copr enable winehq/wine-staging
```

### openSUSE Tumbleweed / Leap

```bash
sudo zypper install wine winetricks python3
```

### NVIDIA / Intel

Não há nada específico a fazer: o Archicad 29 usa OpenGL (o Wine traduz para
OpenGL nativo, não para Vulkan/DXVK). Em NVIDIA com driver proprietário,
garanta que `glxinfo -B` mostre renderer direto (sem llvmpipe).

Em **AMD**, se o OpenGL reclamar de `gfx1031`, exporte antes de abrir:

```bash
export HSA_OVERRIDE_GFX_VERSION=10.3.0
```

---

## 3. Instalação (script)

```bash
git clone https://github.com/prclskrmr/archicad-wine.git
cd archicad-wine
chmod +x scripts/*.sh

# instala tudo e, se o Archicad ainda não estiver instalado, abre o instalador
./scripts/install.sh --installer ~/Downloads/Archicad-29-INT.exe
```

O script:

1. detecta a distro e confere `wine`, `winetricks`, `python3`;
2. cria `~/.local/share/wineprefixes/archicad29` (WINEPREFIX configurável);
3. instala `vcrun2022` + `corefonts` no prefixo;
4. executa o instalador do Archicad (se `--installer` for passado e o
   Archicad ainda não estiver instalado);
5. instala o `rsproxy.dll` (usa `dist/rsproxy.dll` — binário testado — ou
   compila com `mingw-w64` se `--build` for passado);
6. aplica o patch de import em `RS.dll` e `GSRoot.dll` (backup `.orig`);
7. grava as chaves de registro do GS;
8. instala o launcher em `~/.local/bin/archicad29`.

Opções úteis:

```bash
./scripts/install.sh --help
./scripts/install.sh --prefix ~/wine/archicad29     # outro prefixo
./scripts/install.sh --build                        # compila rsproxy do fonte
./scripts/install.sh --skip-winetricks              # prefixo já preparado
```

### Instalação manual (para entender o que o script faz)

```bash
export WINEPREFIX=~/.local/share/wineprefixes/archicad29
export WINEARCH=win64
export WINEDEBUG=-all

wineboot -u
winetricks --unattended vcrun2022 corefonts
wine ~/Downloads/Archicad-29-INT.exe        # instalador gráfico, Next/Next

AC="$WINEPREFIX/drive_c/Program Files/Graphisoft/Archicad 29"

# patch de import (cria RS.dll.orig / GSRoot.dll.orig)
python3 scripts/patch-mui.py "$AC/RS.dll" "$AC/GSRoot.dll"

# proxy de recursos + shim de CRT
cp dist/rsproxy.dll "$AC/"

# registro
wine reg add 'HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Bug reporting' \
     /v disableIncoherentSharedLibraryAlert /t REG_DWORD /d 1 /f
wine reg add 'HKCU\Software\GRAPHISOFT\Archicad\Archicad 29.0.0 INT R1\Bug reporting' \
     /v disableOOPReporting /t REG_DWORD /d 1 /f
```

---

## 4. Primeira execução

```bash
archicad29                                          # tela inicial
archicad29 "Z:\\home\\$USER\\projeto.pln"           # abre um projeto direto
```

Comportamento esperado (não é travamento):

| Tempo | O que acontece |
|---|---|
| 0–10 s | splash "Archicad 29" |
| ~30 s–2 min | janela "About" / carregando licença e add-ons |
| 2–4 min | projeto/interface pronta |
| 5–15 s extra | primeira renderização de paletas e ícones |

Se quiser acompanhar sem chutar: `tools/acstate.sh` mostra se o processo existe
e quais janelas estão abertas. **Não** use `ps comm` — o CEF renomeia a thread
principal para `CrBrowserMain`.

---

## 5. Desinstalar / reverter

```bash
./scripts/uninstall.sh              # remove launcher + desfaz patches (DLLs)
rm -rf ~/.local/share/wineprefixes/archicad29   # apaga o prefixo inteiro
```

Reverter só o patch de import, mantendo a instalação:

```bash
python3 scripts/patch-mui.py --restore "$AC/RS.dll" "$AC/GSRoot.dll"
```
