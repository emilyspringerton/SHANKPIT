# ---- the clean-room PARENA game (apps/game) -----------------------------------------------------
# The rest of this repo builds its PARENA modules through bazel/parena.bzl. That is still the
# canonical build, but it is not available on every node (`which bazel` -> none here) and the
# clean-room game has C host glue the current parena_binary rule does not describe yet, so this
# fragment compiles the same .prn sources with the same compiler (`../PARENA/parena build`) and links
# the host against the emitted C. Nothing generated is checked in: build/game_rules.c is a build
# artifact, exactly as parena.bzl would produce it.
PARENA_BIN ?= ../PARENA/parena

GAME_PRN := packages/game/core_loop.prn \
            packages/game/render_plan.prn \
            packages/game/net_rules.prn \
            packages/game/gameplay_mod.prn \
            packages/game/level_mods.prn \
            packages/sim/combat_api.prn \
            packages/sim/hit_rules.prn \
            packages/sim/shield_rules.prn \
            packages/frame/frame_rules.prn \
            packages/render/palette_rules.prn

GAME_GEN    := build/game_rules.c
GAME_CFLAGS := -O2 -Wall -Wextra -Wno-unused-parameter -D_REENTRANT \
               -Ipackages/reflux -Ipackages/game -Iapps/game

# SDL is the one dependency that differs per platform, so its flags are overridable: the Windows CI
# job passes the mingw SDL2 development kit instead of the host's pkg-config output.
GAME_SDL_CFLAGS ?= $(shell pkg-config --cflags sdl2)
GAME_SDL_LIBS   ?= $(shell pkg-config --libs sdl2)
GAME_EXTRA_LIBS ?= -lm
GAME_BIN      := $(BIN_DIR)/shank_game
GAME_TEST_BIN := $(BIN_DIR)/shank_game_test

.PHONY: game game-test game-smoke game-net-smoke game-level-relay-smoke

game: $(GAME_BIN)

$(GAME_GEN): $(GAME_PRN) $(PARENA_BIN)
	@mkdir -p build
	$(PARENA_BIN) build $(GAME_PRN) -o $@

$(GAME_BIN): $(GAME_GEN) apps/game/host.c apps/game/game_api.h \
             apps/game/level_catalog.c apps/game/level_catalog.h \
             apps/game/event_log.c apps/game/event_log.h \
             packages/game/level_mods_host.c packages/game/level_mods_host.h \
             packages/reflux/reflux_runtime.c packages/reflux/reflux_runtime.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(GAME_CFLAGS) $(GAME_SDL_CFLAGS) \
	  apps/game/host.c apps/game/level_catalog.c apps/game/event_log.c \
	  packages/game/level_mods_host.c packages/reflux/reflux_runtime.c $(GAME_GEN) \
	  -o $@ $(GAME_SDL_LIBS) $(GAME_EXTRA_LIBS)

$(GAME_TEST_BIN): $(GAME_GEN) apps/game/game_test.c apps/game/game_api.h \
                  apps/game/level_catalog.c apps/game/level_catalog.h \
                  apps/game/event_log.c apps/game/event_log.h \
                  packages/game/level_mods_host.c packages/game/level_mods_host.h \
                  packages/reflux/reflux_runtime.c packages/reflux/reflux_runtime.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(GAME_CFLAGS) apps/game/game_test.c apps/game/level_catalog.c apps/game/event_log.c \
	  packages/game/level_mods_host.c packages/reflux/reflux_runtime.c $(GAME_GEN) -o $@ -lm

game-test: $(GAME_TEST_BIN)
	./$(GAME_TEST_BIN)

# No window: runs a real round of the default mode (zombies) for 240 ticks and prints the state the
# rules produced. Used as the build smoke test in CI and after every change to the modules.
game-smoke: $(GAME_BIN)
	./$(GAME_BIN) --headless 240

# Two real processes over loopback UDP: host on 7791, client joins for 120 frames, then both exit.
game-net-smoke: $(GAME_BIN)
	@rm -f /tmp/shank_net_host.log /tmp/shank_net_client.log
	./$(GAME_BIN) --host 7791 --headless 900 > /tmp/shank_net_host.log 2>&1 & \
	  HOST_PID=$$!; sleep 1; \
	  ./$(GAME_BIN) --join 127.0.0.1 7791 --headless 120 > /tmp/shank_net_client.log 2>&1; \
	  CLIENT_STATUS=$$?; wait $$HOST_PID; HOST_STATUS=$$?; \
	  cat /tmp/shank_net_host.log; cat /tmp/shank_net_client.log; \
	  test $$CLIENT_STATUS -eq 0 -a $$HOST_STATUS -eq 0

# Two real processes with DIFFERENT level environments: the server runs a moon bunker (mask 3), the
# client joins as a surface level (mask 31). The client must finish with the server's mask, which is
# only possible if the level-mod packet actually crossed the wire and was applied.
game-level-relay-smoke: $(GAME_BIN)
	@rm -f /tmp/shank_relay_host.log /tmp/shank_relay_client.log
	./$(GAME_BIN) --host 7792 --headless 600 --level 5 --env moon > /tmp/shank_relay_host.log 2>&1 & \
	  HOST_PID=$$!; sleep 1; \
	  ./$(GAME_BIN) --join 127.0.0.1 7792 --headless 90 --level 5 --env surface > /tmp/shank_relay_client.log 2>&1; \
	  CLIENT_STATUS=$$?; wait $$HOST_PID; HOST_STATUS=$$?; \
	  cat /tmp/shank_relay_host.log; cat /tmp/shank_relay_client.log; \
	  grep -q 'level=5 env=0 mods=3' /tmp/shank_relay_client.log && \
	  test $$CLIENT_STATUS -eq 0 -a $$HOST_STATUS -eq 0
