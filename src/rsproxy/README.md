# rsproxy.dll

Proxy de recursos (fallback de MUI) + shim de CRT para o Archicad 29 no Wine.
Detalhes de funcionamento: [docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md).

| Arquivo | O que é |
|---|---|
| `rsproxy.c` | implementação (~410 linhas) |
| `rsproxy.def` | 6 exports implementados + 173 forwarders para `KERNEL32.*` |

## Build

```bash
make                 # x86_64-w64-mingw32-gcc -O2 -shared
make dist            # atualiza ../../dist/rsproxy.dll
```

## Como o `.def` foi gerado

Listamos todos os imports de `KERNEL32.dll` em `RS.dll` e `GSRoot.dll`
(`llvm-readobj --coff-imports`) e exportamos cada um como forwarder:

```
  AcquireSRWLockExclusive=KERNEL32.AcquireSRWLockExclusive
  ...
```

Assim o redirect de import não precisa mudar nenhum outro byte das DLLs: tudo
que elas importavam de `KERNEL32` continua existindo, agora via `rsproxy`.

As 6 funções implementadas de verdade:

```
FindResourceW  FindResourceExW  SizeofResource
LoadResource   LockResource     FreeResource
```

Se uma versão futura do Archicad importar algo novo de `KERNEL32`, o carregador
falha com `import_dll Library rsproxy.dll not found` (função ausente). Nesse
caso, adicione o forwarder correspondente ao `.def` e recompile.

## Variáveis de ambiente

| Variável | Efeito |
|---|---|
| `RSPROXY_VERBOSE=1` | loga todas as consultas de recurso em `/tmp/rsproxy.log` |
| `RSPROXY_SANITIZE=1` | `%n` → `%.0s`; **derruba o app**, apenas para experimentos |
