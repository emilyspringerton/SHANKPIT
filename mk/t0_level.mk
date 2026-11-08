# T0 -- BRAWLPIT as a WASM level (docs/T0_LEVEL_WASM_CONTRACT.md). Founder real-time 2026-11-08: "implement
# as always mods first parena first mods driven development". Sources live in the main Makefile's
# LEVEL_LOBBY_SRC (so `print-lobby-src`, CI and check-windows see them); the targets are here.
#
#   make test-wasm-host       the host API against hand-assembled modules + the real BRAWLPIT module
#                             (WH_FIXTURE=<path>, default ../WOTAN/BRAWLPIT/brawlpit.wasm; skipped if absent)
#   make test-level-abi       the level's REFLUX imports (range-checked) + exports, against hand-assembled
#                             modules and the committed assets/levels/brawlpit/brawlpit_level.wasm
#   make test-level-host      one level's whole life headless: boot, Shift+M toggle/pause, close, reload,
#                             assets, the fixed step, every load failure, traps (the committed module + fixtures)
#   make test-level-manager   the level manager's decisions (PARENA level_manager_rules.prn, generated C)
#   make regen-level-manager  PARENA/stdlib/shankpit/level_manager_rules.prn -> packages/simulation/level_manager_rules.c
#   make level-smoke          the real lobby under Xvfb, real X key events: boot in front, Shift+M, keys reach
#                             BRAWLPIT, Shift+X closes, Shift+M reloads (needs Xvfb + python-xlib + Pillow)
#   make check-strict-t0      every new T0 host file at the doctrine bar, gcc and (if present) mingw
#   make regen-wasm-surface   PARENA/stdlib/wasm/surface.prn -> packages/wasm_host/wasm_surface_gen.c
#   make check-wasm-fixtures  the hand-assembled test modules match scripts/gen_wasm_test_fixtures.py
#   make brawlpit-level       rebuild assets/levels/brawlpit/ (the module from ../PARENA's level.manifest,
#                             the art from ../WOTAN); needs the LLVM 18 toolchain (MONOREPO BOOTSTRAP.md)
#   make check-brawlpit-level fail if the committed module or art differs from a fresh build (skips loudly
#                             without ../PARENA or the toolchain)

STRICT_CFLAGS := -std=c99 -Wall -Wextra -pedantic -Werror
T0_STRICT_FILES := $(T0_HOST_SRC) packages/wasm_host/wasm_host_test.c packages/wasm_host/level_abi_test.c packages/wasm_host/level_host_test.c packages/simulation/level_manager_test.c
WASM3_CFLAGS := -std=c99 -O2 -w
T0_BIN := $(BIN_DIR)/t0
WASM3_OBJS := $(patsubst third_party/wasm3/source/%.c,$(T0_BIN)/wasm3/%.o,$(WASM3_SRC))

.PHONY: level-smoke check-wasm-fixtures test-wasm-host test-level-abi test-level-host test-level-manager regen-level-manager check-strict-t0 regen-wasm-surface brawlpit-level check-brawlpit-level

$(T0_BIN)/wasm3/%.o: third_party/wasm3/source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(WASM3_CFLAGS) -c $< -o $@

test-wasm-host: $(WASM3_OBJS)
	$(CC) $(STRICT_CFLAGS) -O2 -Ipackages/world packages/wasm_host/wasm_host_test.c \
		packages/wasm_host/wasm_host.c packages/wasm_host/wasm_surface_gen.c packages/world/parena_runtime.c \
		$(WASM3_OBJS) -o $(T0_BIN)/wasm_host_test -lm
	./$(T0_BIN)/wasm_host_test

test-level-abi: $(WASM3_OBJS)
	$(CC) $(STRICT_CFLAGS) -O2 -Ipackages/world packages/wasm_host/level_abi_test.c packages/wasm_host/level_abi.c \
		packages/wasm_host/wasm_host.c packages/wasm_host/wasm_surface_gen.c packages/world/parena_runtime.c \
		packages/reflux/reflux_runtime.c $(WASM3_OBJS) -o $(T0_BIN)/level_abi_test -lm
	./$(T0_BIN)/level_abi_test

test-level-host: $(WASM3_OBJS)
	$(CC) $(STRICT_CFLAGS) -O2 -Ipackages/world packages/wasm_host/level_host_test.c packages/wasm_host/level_host.c \
		packages/wasm_host/level_abi.c packages/wasm_host/wasm_host.c packages/wasm_host/wasm_surface_gen.c \
		packages/simulation/level_manager_rules.c packages/world/png_decode_gen.c packages/world/parena_runtime.c \
		packages/reflux/reflux_runtime.c $(WASM3_OBJS) -o $(T0_BIN)/level_host_test -lm
	./$(T0_BIN)/level_host_test

test-level-manager:
	@mkdir -p $(T0_BIN)
	$(CC) $(STRICT_CFLAGS) -O2 -Ipackages/world packages/simulation/level_manager_test.c \
		packages/simulation/level_manager_rules.c -o $(T0_BIN)/level_manager_test
	./$(T0_BIN)/level_manager_test

regen-level-manager:
	scripts/gen_level_manager_rules.sh

# Each file is COMPILED at -O1 and -O2 (not -fsyntax-only): -Wformat-truncation, -Wmaybe-uninitialized and the
# stringop checks live in the optimiser, so a syntax-only pass never sees them (found: a message buffer in
# wasm_host.c that only an -O1 build flagged).
check-strict-t0:
	@for f in $(T0_STRICT_FILES); do \
		for o in -O1 -O2; do \
			$(CC) $(STRICT_CFLAGS) $$o $(INCLUDES) -c $$f -o /dev/null || exit 1; \
			if command -v $(MINGW_CC) >/dev/null 2>&1; then \
				$(MINGW_CC) $(STRICT_CFLAGS) $$o $(INCLUDES) $(if $(SDL2_MINGW_PREFIX),-I$(SDL2_MINGW_PREFIX)/include) -c $$f -o /dev/null || exit 1; \
			fi; \
		done; \
	done; echo "strict bar: $(words $(T0_STRICT_FILES)) T0 files clean at -O1 and -O2 (gcc$$(command -v $(MINGW_CC) >/dev/null 2>&1 && echo ' + mingw'))"

regen-wasm-surface:
	scripts/gen_wasm_surface.sh

brawlpit-level:
	scripts/build_brawlpit_level.sh

check-brawlpit-level:
	scripts/build_brawlpit_level.sh --check

check-wasm-fixtures:
	python3 -I scripts/gen_wasm_test_fixtures.py --check

level-smoke:
	scripts/level_smoke_test.sh
