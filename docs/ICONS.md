# Ícones da Toolbox: investigação

**Status:** aberto. O diagnóstico está feito e as hipóteses erradas já foram
eliminadas — falta encontrar o caminho de renderização.

**Sintoma:** na paleta *Toolbox* (a coluna de ferramentas: Parede, Porta,
Janela, Pilar, Viga…), alguns botões aparecem sem ícone. As barras de ferramentas,
os menus e os ícones de diálogo aparecem normalmente.

---

## 1. De onde vêm os ícones

Varredura de recursos PE de toda a instalação
([`tools/peicons.py`](../tools/peicons.py)):

| Módulo | `RT_ICON` | `RT_GROUP_ICON` | Observação |
|---|---|---|---|
| `Archicad.exe` | 11.442 | 11.231 | fonte principal |
| `ACUserInterface.dll` | 1.123 | 1.123 | UI nova (paletas, propriedades) |
| `DG.dll` | 49 | 49 | framework de desenho |
| `CommonIcons.dll` | 4 | 4 | só `I31000`, `I31010` |
| `en/*.mui` (96 arquivos) | **0** | **0** | satélites não têm ícones |

Nomenclatura: cada ícone tem duas variantes de DPI —

```
I<id>&96     16/32 px   (escala 100%)
I<id>&192    32/48 px   (escala 200%)
```

Cobertura das variantes (não há buraco de DPI que explique ícones faltando):

| Módulo | bases `I<id>` | com `&96`+`&192` | só `&96` | só `&192` |
|---|---|---|---|---|
| `Archicad.exe` | 5.669 | 5.505 | 164 | 0 |
| `ACUserInterface.dll` | 564 | 559 | 5 | 0 |
| `DG.dll` | 25 | 24 | 1 | 0 |

Formato dos dados: **todos** são PNG 8-bit RGBA (color type 6, sem entrelace)
embutidos em `RT_ICON`, com o `ICONDIR` do grupo declarando `0x0` (o tamanho real
está no IHDR do PNG). Não existe um "formato esquisito" separando os que
funcionam dos que não funcionam.

---

## 2. O que já foi descartado

### 2.1 Não é falha de `LoadImage`

[`tools/iconprobe.c`](../tools/iconprobe.c) carrega **todos** os grupos de ícones
de um módulo com `LoadImageW(…, IMAGE_ICON, …)`, pega o `HICON`, extrai o bitmap
com `GetDIBits` e mede os pixels:

```
== Archicad.exe:       group icons=11231 loadfail=1 blank=23 ok=11207
== ACUserInterface.dll: group icons=1123  loadfail=0 blank=0  ok=1123
== CommonIcons.dll:     group icons=4     loadfail=0 blank=0  ok=4
```

Os 23 "blank" foram inspecionados individualmente: são PNG **pretos com alfa**
(`rgb_mean = 0`, `alpha_mean > 0`) — silhuetas que o GS colore conforme o tema.
São visíveis; o teste só olhava RGB. O único realmente vazio é `I29641&96/192`
(alfa 0 em todos os pixels). Portanto **a decodificação de ícone do Wine está OK**.

### 2.2 Não é o proxy de recursos

Com `RSPROXY_VERBOSE=1`, o log do proxy não registra **nenhuma** consulta de
`ICON`/`BITMAP`/`GROUP_ICON` vindo do GS — todas são `STRS`, `TOCF`, `TOCL`,
`JSNL`, `FILE`, `MDID`… Ou seja, o caminho de ícones não passa por
`FindResource` (e, se passasse, o fallback MUI não teria o que achar: as `.mui`
não têm ícones).

### 2.3 Não é variante de DPI

Ver tabela da seção 1: todo ícone tem variante `&96`, e o prefixo não define
`LogPixels` (`HKCU\Control Panel\Desktop\LogPixels` ausente → 96 DPI).

### 2.4 Não é "faltou recurso"

Os ícones existem no módulo (`Archicad.exe` / `ACUserInterface.dll`) e o Wine os
decodifica. Logo, o problema está **depois** do carregamento.

---

## 3. Hipóteses restantes (em ordem de probabilidade)

1. **Caminho Direct2D/WIC do `DG`** — o GS novo desenha a UI com Direct2D
   (`D2DWRenderManager: Creating` aparece no FastLogger). Os bitmaps de ícone
   podem ser criados via `CreateBitmapFromWicBitmap`/`CreateBitmap` com um
   formato que o `d2d1` do Wine rejeita silenciosamente (retorna bitmap vazio).
2. **`AlphaBlend`/`ImageList` com DIB de 32 bpp e RGB todo zero** — silhueta
   preta + alfa: se o Wine tratar o RGB como "cor" e o alfa como "opacidade"
   sem pré-multiplicar, alguns ícones podem sumir em fundo escuro.
3. **Layout da paleta** — a Toolbox tem dois modos (ícone+texto / só texto).
   Em modo dockado estreito os ícones somem por decisão da UI, não por bug. Vale
   confirmar antes de caçar bug no Wine.
4. **`DrawIconEx`/`CreateIconFromResourceEx` com `ICONDIR` 0x0** — todos os
   grupos declaram tamanho `0x0` (256 px). Se o GS pedir o ícone com
   `cx=cy=0`, o Wine pode criar um HICON de 256×256 com a imagem pequena
   esticada — borrado, mas não invisível. Improvável ser a causa.

---

## 4. Como continuar (próximos passos concretos)

### 4.1 Identificar exatamente quais ferramentas falham

```bash
# com o Archicad aberto e a Toolbox visível:
tools/acstate.sh                    # confirma processo/janelas
grim /tmp/ac.png                    # ou: import -window root /tmp/ac.png
```

Na imagem, recorte cada botão da Toolbox e compare desvio-padrão/contagem de
cores; botão "vazio" tem desvio ~0. Anote o **nome da ferramenta** de cada célula
(passando o mouse e lendo o tooltip com `xdotool getwindowname`) e abra uma issue
com a lista — isso permite cruzar os IDs `I<id>` das ferramentas afetadas, que é
o dado que falta para achar a causa.

### 4.2 Provar/derrubar a hipótese Direct2D

```bash
# 1) desabilita d2d1 (o GS deve cair para GDI):
WINEDLLOVERRIDES="d2d1=d" archicad29
# 2) alternativa: força render por software no GS, se houver opção em
#    Options > Work Environment > On-Screen View Options
```

Se os ícones aparecerem com `d2d1=d`, o bug é no `d2d1` do Wine (reportar
upstream com o PNG de exemplo em anexo — veja 4.3).

### 4.3 Extrair PNGs de exemplo para teste isolado

```bash
python3 tools/peicons.py --dump "$AC/Archicad.exe" /tmp/ico   # extrai ICON/GROUP_ICON
```

Com um PNG extraído, testar os dois caminhos num programa mínimo:

```
WIC:     IWICImagingFactory::CreateDecoderFromFilename + CreateFormatConverter
GDI+:    GdipCreateBitmapFromFile + GdipCreateHBITMAPFromBitmap
D2D:     CreateBitmapFromWicBitmap → GetSize / CopyFromBitmap
```

Qualquer um que devolva bitmap vazio/erro para um PNG que o `magick` abre
normalmente é o culpado.

### 4.4 Hook de diagnóstico no proxy

Adicionar ao `rsproxy.c` (é o próximo incremento natural do projeto) hooks em
`LoadImageW`, `LoadIconW`, `CreateIconFromResourceEx` e `LoadBitmapW` que logam
`(name, type, cx, cy, retorno)` — com isso se vê **se** o GS pede aquele ícone e
**o que** recebe. Se o pedido nem acontece, o problema é decisão da UI; se
acontece e o retorno é válido, o problema é desenho.

---

## 5. Ferramentas deste repositório

| Ferramenta | Uso |
|---|---|
| [`tools/peicons.py`](../tools/peicons.py) | lista tipos/contagem de recursos PE de um diretório |
| [`tools/iconprobe.c`](../tools/iconprobe.c) | carrega todos os `RT_GROUP_ICON` de um módulo e reporta falhas/vazios |
| [`tools/acstate.sh`](../tools/acstate.sh) | estado do processo/janelas (nomes reais, não `ps comm`) |

Compilar o probe:

```bash
x86_64-w64-mingw32-gcc -O1 -municode -o iconprobe.exe tools/iconprobe.c -lgdi32 -luser32
WINEPREFIX=~/.local/share/wineprefixes/archicad29 wine iconprobe.exe \
  "C:\\Program Files\\Graphisoft\\Archicad 29\\Archicad.exe"
```
