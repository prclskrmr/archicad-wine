# Como contribuir

Obrigado pelo interesse. Este projeto existe porque o Archicad 29 **quase**
roda em Wine e as quatro barreiras já estão resolvidas — o que falta é
justamente o que outras máquinas/distros/GPUs vão revelar.

## Antes de abrir uma issue

1. Leia [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) e
   [ROADMAP.md](ROADMAP.md) (talvez já seja item conhecido).
2. Rode a coleta de diagnóstico de
   [docs/TESTING.md](docs/TESTING.md#coleta-de-diagnóstico-para-issue).
3. Use o template — sem `wine --version` e o estado do prefixo não dá para
   reproduzir.

## Fluxo de contribuição

```bash
git clone https://github.com/prclskrmr/archicad-wine.git
cd archicad-wine
git checkout -b fix/descricao-curta
# ... alterações ...
git commit -m "fix(icones): ..."     # use conventional commits
git push origin fix/descricao-curta
# abra o Pull Request descrevendo o teste feito
```

## O que um PR precisa ter

- **Teste manual descrito**: o que você rodou, em qual distro, qual Wine, qual
  GPU, e o resultado (antes/depois).
- Se mudou `src/rsproxy`: `make -C src/rsproxy check` passando e
  `dist/rsproxy.dll` atualizado (`make -C src/rsproxy dist`).
- Se mudou `scripts/`: `shellcheck` limpo e `bash -n` ok.
- Se mudou Python: `python3 -m py_compile` ok.
- Texto/comentários em **pt-BR** quando o arquivo já é pt-BR.

```bash
bash -n scripts/*.sh
shellcheck -S warning scripts/*.sh
python3 -m py_compile scripts/patch-mui.py tools/peicons.py
make -C src/rsproxy check
```

## Ideias onde ajuda é bem-vinda

| Área | Dificuldade | Onde começar |
|---|---|---|
| Ícones da Toolbox | média/alta | [docs/ICONS.md](docs/ICONS.md) — diagnóstico pronto, falta o hook |
| CPU ~70% (CEF/repaint) | média | `top -H -p $(pgrep -x Archicad.exe)` |
| Crash combase/DG (OLE) | alta | `WINEDEBUG=+ole,+dragdrop` |
| Empacotamento `.deb`/Flatpak/Nix | baixa | `packaging/PKGBUILD` como modelo |
| Localizar slot de CRT por assinatura | média | [ROADMAP §4](ROADMAP.md) |
| Formatar `%n` no shim | média | [ROADMAP §5](ROADMAP.md) |
| Testes em NVIDIA/Intel/Wayland | baixa | rodar `docs/TESTING.md` e reportar |

## Estilo

- Scripts: `bash` com `set -euo pipefail`, sem dependência de `bash` 5.
- C: C99, sem dependências além das DLLs do sistema (o proxy é carregado em
  processo alheio — quanto menos, melhor).
- Nada de `pkill -f <padrão>` em script: mate por PID (`pgrep -x`), porque o
  padrão casa com a própria linha de comando do shell.
- Não edite `/usr/share/omarchy` nem arquivos de pacote: personalize em
  `~/.config` / `~/.local`.

## Licença

Ao contribuir você concorda em licenciar o código sob **MIT** (ver
[LICENSE](LICENSE)).
