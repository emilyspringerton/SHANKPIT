# T0 -- BRAWLPIT as a WASM level (docs/T0_LEVEL_WASM_CONTRACT.md). Founder real-time 2026-11-08: "implement
# as always mods first parena first mods driven development". Sources live in the main Makefile's
# LEVEL_LOBBY_SRC (so `print-lobby-src`, CI and check-windows see them); the targets are here.
#
#   make test-wasm-host       the host API against hand-assembled modules + the real BRAWLPIT module
#                             (WH_FIXTURE=<path>, default ../WOTAN/BRAWLPIT/brawlpit.wasm; skipped if absent)
#   make check-strict-t0      every new T0 host file at the doctrine bar, gcc and (if present) mingw
#   make regen-wasm-surface   PARENA/stdlib/wasm/surface.prn -> packages/wasm_host/wasm_surface_gen.c
#   make brawlpit-level       rebuild assets/levels/brawlpit/ (the module from ../PARENA's level.manifest,
#                             the art from ../WOTAN); needs the LLVM 18 toolchain (MONOREPO BOOTSTRAP.md)
#   make check-brawlpit-level fail if the committed module or art differs from a fresh build (skips loudly
#                             without ../PARENA or the toolchain)

STRICT_CFLAGS := -std=c99 -Wall -Wextra -pedantic -Werror
T0_STRICT_FILES := $(T0_HOST_SRC) packages/wasm_host/wasm_host_test.c
WASM3_CFLAGS := -std=c99 -O2 -w
T0_BIN := $(BIN_DIR)/t0
WASM3_OBJS := $(patsubst third_party/wasm3/source/%.c,$(T0_BIN)/wasm3/%.o,$(WASM3_SRC))

.PHONY: test-wasm-host check-strict-t0 regen-wasm-surface brawlpit-level check-brawlpit-level

$(T0_BIN)/wasm3/%.o: third_party/wasm3/source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(WASM3_CFLAGS) -c $< -o $@

test-wasm-host: $(WASM3_OBJS)
	$(CC) $(STRICT_CFLAGS) -O2 -Ipackages/world packages/wasm_host/wasm_host_test.c \
		packages/wasm_host/wasm_host.c packages/wasm_host/wasm_surface_gen.c packages/world/parena_runtime.c \
		$(WASM3_OBJS) -o $(T0_BIN)/wasm_host_test -lm
	./$(T0_BIN)/wasm_host_test

check-strict-t0:
	@for f in $(T0_STRICT_FILES); do \
		$(CC) $(STRICT_CFLAGS) $(INCLUDES) -fsyntax-only $$f || exit 1; \
		if command -v $(MINGW_CC) >/dev/null 2>&1; then \
			$(MINGW_CC) $(STRICT_CFLAGS) $(INCLUDES) $(if $(SDL2_MINGW_PREFIX),-I$(SDL2_MINGW_PREFIX)/include) -fsyntax-only $$f || exit 1; \
		fi; \
	done; echo "strict bar: $(words $(T0_STRICT_FILES)) T0 files clean (gcc$$(command -v $(MINGW_CC) >/dev/null 2>&1 && echo ' + mingw'))"

regen-wasm-surface:
	scripts/gen_wasm_surface.sh

brawlpit-level:
	scripts/build_brawlpit_level.sh

check-brawlpit-level:
	scripts/build_brawlpit_level.sh --check
