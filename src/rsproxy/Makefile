# Build do rsproxy.dll (cross-compile com mingw-w64)
#
#   make            -> rsproxy.dll
#   make check      -> confere exports/forwarders do .def
#   make dist       -> copia o binario para ../../dist/
#   make clean

MINGW  ?= x86_64-w64-mingw32-gcc
OBJDUMP ?= x86_64-w64-mingw32-objdump
CFLAGS ?= -O2 -Wall -Wextra
DLL    := rsproxy.dll

.PHONY: all check dist clean

all: $(DLL)

$(DLL): rsproxy.c rsproxy.def
	$(MINGW) $(CFLAGS) -shared -o $@ rsproxy.c rsproxy.def
	@echo "built: $@ ($$(stat -c%s $@) bytes)"

# Sanidade: o .def deve exportar tudo que RS.dll e GSRoot.dll importam de
# KERNEL32. Rode contra os originais se eles estiverem por perto.
check: $(DLL)
	@echo "-- exports: $$($(OBJDUMP) -p $(DLL) | grep -cE '^\s+\[[[:space:]]*[0-9]+\]')"
	@dupes=$$(grep -oE '^  [A-Za-z_][A-Za-z0-9_@]*' rsproxy.def | sort | uniq -d); \
	if [ -n "$$dupes" ]; then echo "!! exports duplicados: $$dupes"; exit 1; fi
	@echo "-- ok"

dist: $(DLL)
	install -m 755 $(DLL) ../../dist/$(DLL)
	@echo "copiado para dist/$(DLL)"

clean:
	rm -f $(DLL)
