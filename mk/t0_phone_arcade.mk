# mk/phone_arcade.mk -- the SHANKPHONE's PELLEGRINO games app (2026-11-14). The games are a PARENA mod
# (stdlib/shankpit/arcade_games.prn), the app's behaviour PARENA rules (arcade.prn), both generated into
# packages/simulation/arcade_rules.c; the state is packages/simulation/phone_arcade.h.

.PHONY: regen-arcade-rules test-phone-arcade regen-chromium-bridge test-phone-chromium

regen-arcade-rules:
	scripts/gen_arcade_rules.sh

# test-phone-arcade -- the list, the browser row, a web game with and without Chromium, and the REAL BRAWLPIT module
# loaded, rendered, stepped and closed on the phone's own wasm3 host.
test-phone-arcade: $(WASM3_OBJS)
	@mkdir -p $(T0_BIN)
	$(CC) $(STRICT_CFLAGS) -O2 -Ipackages/world packages/simulation/phone_arcade_test.c packages/simulation/arcade_rules.c \
		packages/wasm_host/level_host.c packages/wasm_host/level_abi.c packages/wasm_host/wasm_host.c \
		packages/wasm_host/wasm_surface_gen.c packages/simulation/level_manager_rules.c packages/world/png_decode_gen.c \
		packages/world/parena_runtime.c packages/reflux/reflux_runtime.c $(WASM3_OBJS) -o $(T0_BIN)/phone_arcade_test -lm
	./$(T0_BIN)/phone_arcade_test

# The web-game STOPGAP (packages/chromium_bridge/phone_chromium.c): PARENA's WebDriver client driving headless
# Chromium. Regenerate the PARENA half with scripts/gen_chromium_bridge.sh. The test SKIPs without a chromedriver.
regen-chromium-bridge:
	bash scripts/gen_chromium_bridge.sh

test-phone-chromium:
	@mkdir -p $(T0_BIN)
	$(CC) -std=gnu99 -O1 -w -Ipackages/chromium_bridge packages/chromium_bridge/phone_chromium_test.c \
		packages/chromium_bridge/phone_chromium.c packages/simulation/arcade_rules.c packages/world/parena_runtime.c \
		-Ipackages/world -o $(T0_BIN)/phone_chromium_test -lpthread -lm
	./$(T0_BIN)/phone_chromium_test
