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
            packages/game/build_profiles.prn \
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
CORE_BIN      := $(BIN_DIR)/shank_core
WASM_BIN      := $(BIN_DIR)/shank_core.wasm
WASM_CC       ?= clang

# Every client compiles apps/game/build_profile.c with one target from build_profiles.prn. A new
# target adds its table row and one variable here; no main() gets another hard-coded profile call.
GAME_PROFILE      ?= bp_target_native
GAME_TEST_PROFILE ?= bp_target_native
CORE_PROFILE      ?= bp_target_native_core
WASM_PROFILE      ?= bp_target_wasm_core

.PHONY: game game-test game-smoke game-stream-smoke game-net-smoke game-level-relay-smoke \
        game-level-request-smoke game-mod-authority-smoke game-capped-client-smoke \
        game-level-interest-smoke game-multi-peer-smoke game-clang game-llvm game-test-clang \
        core core-smoke wasm wasm-smoke

game: $(GAME_BIN)

$(GAME_GEN): $(GAME_PRN) $(PARENA_BIN)
	@mkdir -p build
	$(PARENA_BIN) build $(GAME_PRN) -o $@

$(GAME_BIN): $(GAME_GEN) apps/game/host.c apps/game/game_api.h \
             apps/game/build_profile.c apps/game/build_profile.h \
             apps/game/net_transport.h apps/game/net_transport_udp.c \
             apps/game/level_catalog.c apps/game/level_catalog.h \
             apps/game/event_log.c apps/game/event_log.h \
             packages/game/level_mods_host.c packages/game/level_mods_host.h \
             packages/reflux/reflux_runtime.c packages/reflux/reflux_runtime.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(GAME_CFLAGS) -DSHANKPIT_BUILD_TARGET=$(GAME_PROFILE) $(GAME_SDL_CFLAGS) \
	  apps/game/host.c apps/game/build_profile.c apps/game/net_transport_udp.c \
	  apps/game/level_catalog.c apps/game/event_log.c \
	  packages/game/level_mods_host.c packages/reflux/reflux_runtime.c $(GAME_GEN) \
	  -o $@ $(GAME_SDL_LIBS) $(GAME_EXTRA_LIBS)

$(GAME_TEST_BIN): $(GAME_GEN) apps/game/game_test.c apps/game/game_api.h \
                  apps/game/build_profile.c apps/game/build_profile.h \
                  apps/game/level_catalog.c apps/game/level_catalog.h \
                  apps/game/event_log.c apps/game/event_log.h \
                  packages/game/level_mods_host.c packages/game/level_mods_host.h \
                  packages/reflux/reflux_runtime.c packages/reflux/reflux_runtime.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(GAME_CFLAGS) -DSHANKPIT_BUILD_TARGET=$(GAME_TEST_PROFILE) \
	  apps/game/game_test.c apps/game/build_profile.c \
	  apps/game/level_catalog.c apps/game/event_log.c \
	  packages/game/level_mods_host.c packages/reflux/reflux_runtime.c $(GAME_GEN) -o $@ -lm

game-test: $(GAME_TEST_BIN)
	./$(GAME_TEST_BIN)

# The core without the networking mod: same rules/registry, no host.c, no SDL, no sockets.
$(CORE_BIN): $(GAME_GEN) apps/game/core_main.c apps/game/build_profile.c \
             apps/game/build_profile.h packages/game/level_mods_host.c \
             packages/game/level_mods_host.h packages/reflux/reflux_runtime.c \
             packages/reflux/reflux_runtime.h
	@mkdir -p $(BIN_DIR)
	$(CC) $(GAME_CFLAGS) -DSHANKPIT_BUILD_TARGET=$(CORE_PROFILE) \
	  apps/game/core_main.c apps/game/build_profile.c packages/game/level_mods_host.c \
	  packages/reflux/reflux_runtime.c $(GAME_GEN) -o $@ -lm

core: $(CORE_BIN)

core-smoke: $(CORE_BIN)
	@./$(CORE_BIN) --level 9 --env moon | tee /tmp/shank_core_moon.log
	@grep -q 'target=native-core net=none .*mods=3 rev=1' /tmp/shank_core_moon.log
	@./$(CORE_BIN) --level 10 --env surface --capabilities 3 | tee /tmp/shank_core_cap.log
	@grep -q 'caps=3 .*mods=3' /tmp/shank_core_cap.log
	@./$(CORE_BIN) --level 11 --env surface --set-mod 3 0 | tee /tmp/shank_core_shed.log
	@grep -q 'mods=27 rev=2' /tmp/shank_core_shed.log
	@! nm -u $(CORE_BIN) | grep -Eq '\b(socket|bind|sendto|recvfrom|connect|inet_addr|WSAStartup)@'

# The same C through a different native toolchain. Separate output names so a gcc build is never
# clobbered by a clang build in the same tree.
game-clang:
	$(MAKE) -B game CC=clang GAME_BIN=$(BIN_DIR)/shank_game_clang \
	  GAME_PROFILE=bp_target_llvm GAME_CFLAGS="$(GAME_CFLAGS) -Wno-parentheses-equality"

# "Pure LLVM": clang + LTO, linked with lld, so the whole C host goes through LLVM once.
game-llvm:
	$(MAKE) -B game CC=clang GAME_BIN=$(BIN_DIR)/shank_game_llvm \
	  GAME_PROFILE=bp_target_llvm \
	  GAME_CFLAGS="$(GAME_CFLAGS) -Wno-parentheses-equality -flto -fuse-ld=lld" \
	  GAME_EXTRA_LIBS="-lm -flto"

game-test-clang:
	$(MAKE) -B game-test CC=clang GAME_TEST_BIN=$(BIN_DIR)/shank_game_test_clang \
	  GAME_CFLAGS="$(GAME_CFLAGS) -Wno-parentheses-equality"

# The first pure-WASM slice: PARENA rules + REFLUX + the level registry, freestanding (no libc, no
# SDL, no imports). Render/input/audio are still native-only and are declared unported by wasm_boot().
$(WASM_BIN): $(GAME_GEN) apps/game/wasm_core.c apps/game/build_profile.c \
             apps/game/build_profile.h packages/game/level_mods_host.c \
             packages/game/level_mods_host.h packages/reflux/reflux_runtime.c \
             packages/reflux/reflux_runtime.h
	@mkdir -p $(BIN_DIR)
	$(WASM_CC) --target=wasm32 -O2 -nostdlib -fno-builtin -ffunction-sections \
	  -Wno-parentheses-equality -DSHANKPIT_REFLUX_PARENA_RUNTIME_H \
	  -DSHANKPIT_BUILD_TARGET=$(WASM_PROFILE) \
	  -Iapps/game/wasm_include -Ipackages/reflux -Ipackages/game -Iapps/game \
	  -Wl,--no-entry -Wl,--export-all -Wl,--strip-all -Wl,--gc-sections \
	  apps/game/wasm_core.c apps/game/build_profile.c packages/game/level_mods_host.c \
	  packages/reflux/reflux_runtime.c $(GAME_GEN) -o $@

wasm: $(WASM_BIN)

wasm-smoke: $(WASM_BIN)
	node apps/game/wasm_check.mjs

# No window: runs a real round of the default mode (zombies) for 240 ticks and prints the state the
# rules produced. Used as the build smoke test in CI and after every change to the modules.
game-smoke: $(GAME_BIN)
	./$(GAME_BIN) --headless 240

# Two separate host processes append to the same JSONL stream. The ids must continue across the
# restart (1..N) instead of resetting, and replay must accept the resulting file.
game-stream-smoke: $(GAME_BIN)
	@rm -f /tmp/shank_stream_smoke.jsonl
	./$(GAME_BIN) --headless 4 --level 6 --env surface \
	  --event-log /tmp/shank_stream_smoke.jsonl > /tmp/shank_stream_first.log 2>&1
	./$(GAME_BIN) --headless 4 --level 6 --env surface \
	  --event-log /tmp/shank_stream_smoke.jsonl > /tmp/shank_stream_second.log 2>&1
	@python3 -c 'import json; ids=[json.loads(l)["stream"] for l in open("/tmp/shank_stream_smoke.jsonl") if l.strip()]; assert ids == list(range(1, len(ids)+1)) and len(ids) >= 4, ids; print("stream ids:", ids)'
	./$(GAME_BIN) --headless 1 --level 6 --env surface \
	  --replay-events /tmp/shank_stream_smoke.jsonl > /tmp/shank_stream_replay.log 2>&1
	@grep -q 'replayed 0 level mod row' /tmp/shank_stream_replay.log

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
# A live optimization request: the server starts on the surface (mask 31), the client asks it to drop
# weather rendering, and the next level-state packet must carry the new mask (31 -> 27) back.
game-level-request-smoke: $(GAME_BIN)
	@rm -f /tmp/shank_request_host.log /tmp/shank_request_client.log
	./$(GAME_BIN) --host 7794 --headless 600 --level 6 --env surface > /tmp/shank_request_host.log 2>&1 & \
	  HOST_PID=$$!; sleep 1; \
	  ./$(GAME_BIN) --join 127.0.0.1 7794 --headless 120 --level 6 --env surface \
	    --request-mod 3 0 > /tmp/shank_request_client.log 2>&1; \
	  CLIENT_STATUS=$$?; wait $$HOST_PID; HOST_STATUS=$$?; \
	  cat /tmp/shank_request_host.log; cat /tmp/shank_request_client.log; \
	  grep -q 'level=6 env=0 mods=27' /tmp/shank_request_client.log && \
	  grep -q 'mod_policy=owner mod_requests=1 mod_refused=0' /tmp/shank_request_host.log && \
	  test $$CLIENT_STATUS -eq 0 -a $$HOST_STATUS -eq 0

# Request authority, both halves: a peer that sends packet 5 with the wrong token is refused, then a
# peer with the welcome token is accepted. The server stays up across both clients and reports one
# authenticated request and one refusal. `--mod-policy peer` isolates the token check from owner
# selection, which `game-level-request-smoke` already covers.
game-mod-authority-smoke: $(GAME_BIN)
	@rm -f /tmp/shank_auth_host.log /tmp/shank_auth_bad.log /tmp/shank_auth_good.log
	./$(GAME_BIN) --host 7798 --headless 700 --level 6 --env surface --mod-policy peer \
	  > /tmp/shank_auth_host.log 2>&1 & \
	  HOST_PID=$$!; sleep 1; \
	  ./$(GAME_BIN) --join 127.0.0.1 7798 --headless 70 --level 6 --env surface \
	    --request-mod 3 0 --request-token 0 > /tmp/shank_auth_bad.log 2>&1; \
	  BAD_STATUS=$$?; sleep 0.4; \
	  ./$(GAME_BIN) --join 127.0.0.1 7798 --headless 120 --level 6 --env surface \
	    --request-mod 3 0 > /tmp/shank_auth_good.log 2>&1; \
	  GOOD_STATUS=$$?; wait $$HOST_PID; HOST_STATUS=$$?; \
	  cat /tmp/shank_auth_host.log; cat /tmp/shank_auth_bad.log; cat /tmp/shank_auth_good.log; \
	  grep -q 'mod_policy=peer mod_requests=1 mod_refused=1' /tmp/shank_auth_host.log && \
	  grep -q 'level=6 env=0 mods=27' /tmp/shank_auth_good.log && \
	  test $$BAD_STATUS -eq 0 -a $$GOOD_STATUS -eq 0 -a $$HOST_STATUS -eq 0

# A client that has only ported core sim + level (mask 3) joins a surface server (mask 31). The
# client must finish at 3: the server's state crossed the wire, but the local capability clamp held.
game-capped-client-smoke: $(GAME_BIN)
	@rm -f /tmp/shank_cap_host.log /tmp/shank_cap_client.log
	./$(GAME_BIN) --host 7795 --headless 600 --level 8 --env surface > /tmp/shank_cap_host.log 2>&1 & \
	  HOST_PID=$$!; sleep 1; \
	  ./$(GAME_BIN) --join 127.0.0.1 7795 --headless 90 --level 8 --env surface \
	    --capabilities 3 > /tmp/shank_cap_client.log 2>&1; \
	  CLIENT_STATUS=$$?; wait $$HOST_PID; HOST_STATUS=$$?; \
	  cat /tmp/shank_cap_host.log; cat /tmp/shank_cap_client.log; \
	  grep -q 'level=8 env=0 mods=3' /tmp/shank_cap_client.log && \
	  grep -q 'peer_caps=3' /tmp/shank_cap_host.log && \
	  grep -q 'peer_level=8' /tmp/shank_cap_host.log && \
	  test $$CLIENT_STATUS -eq 0 -a $$HOST_STATUS -eq 0

# Two clients at once: different caps and different watched levels. The server must keep two peer
# rows, assign distinct slots, clamp/filter each independently, and each client must learn its slot.
game-multi-peer-smoke: $(GAME_BIN)
	@rm -f /tmp/shank_multi_host.log /tmp/shank_multi_a.log /tmp/shank_multi_b.log
	./$(GAME_BIN) --host 7797 --headless 900 --levels assets/game/levels.json --level 1 \
	  > /tmp/shank_multi_host.log 2>&1 & \
	  HOST_PID=$$!; sleep 1; \
	  ./$(GAME_BIN) --join 127.0.0.1 7797 --headless 150 --level 1 --env surface \
	    > /tmp/shank_multi_a.log 2>&1 & \
	  A_PID=$$!; sleep 0.4; \
	  ./$(GAME_BIN) --join 127.0.0.1 7797 --headless 150 --level 2 --env underground \
	    --capabilities 3 > /tmp/shank_multi_b.log 2>&1 & \
	  B_PID=$$!; \
	  wait $$A_PID; A_STATUS=$$?; wait $$B_PID; B_STATUS=$$?; \
	  wait $$HOST_PID; HOST_STATUS=$$?; \
	  cat /tmp/shank_multi_host.log; cat /tmp/shank_multi_a.log; cat /tmp/shank_multi_b.log; \
	  grep -q 'peers=2' /tmp/shank_multi_host.log && \
	  grep -q 'p0_slot=8 p0_caps=31 p0_level=1 p0_rows=1' /tmp/shank_multi_host.log && \
	  grep -q 'p1_slot=9 p1_caps=3 p1_level=2 p1_rows=1' /tmp/shank_multi_host.log && \
	  grep -q 'slot=8' /tmp/shank_multi_a.log && \
	  grep -q 'level=1 env=0 mods=31' /tmp/shank_multi_a.log && \
	  grep -q 'slot=9' /tmp/shank_multi_b.log && \
	  grep -q 'level=2 env=1 mods=3' /tmp/shank_multi_b.log && \
	  test $$A_STATUS -eq 0 -a $$B_STATUS -eq 0 -a $$HOST_STATUS -eq 0

# Four levels are registered on the server; the client advertises interest in level 2, so the
# server must send one row (level_rows=1), not all four, and the client must adopt mask 11.
game-level-interest-smoke: $(GAME_BIN)
	@rm -f /tmp/shank_interest_host.log /tmp/shank_interest_client.log
	./$(GAME_BIN) --host 7796 --headless 600 --levels assets/game/levels.json --level 2 \
	  > /tmp/shank_interest_host.log 2>&1 & \
	  HOST_PID=$$!; sleep 1; \
	  ./$(GAME_BIN) --join 127.0.0.1 7796 --headless 90 --level 2 --env underground \
	    > /tmp/shank_interest_client.log 2>&1; \
	  CLIENT_STATUS=$$?; wait $$HOST_PID; HOST_STATUS=$$?; \
	  cat /tmp/shank_interest_host.log; cat /tmp/shank_interest_client.log; \
	  grep -q 'peer_level=2' /tmp/shank_interest_host.log && \
	  grep -q 'level_rows=1' /tmp/shank_interest_host.log && \
	  grep -q 'level=2 env=1 mods=11' /tmp/shank_interest_client.log && \
	  test $$CLIENT_STATUS -eq 0 -a $$HOST_STATUS -eq 0

game-level-relay-smoke: $(GAME_BIN)
	@rm -f /tmp/shank_relay_host.log /tmp/shank_relay_client.log
	./$(GAME_BIN) --host 7792 --headless 600 --level 5 --env moon > /tmp/shank_relay_host.log 2>&1 & \
	  HOST_PID=$$!; sleep 1; \
	  ./$(GAME_BIN) --join 127.0.0.1 7792 --headless 90 --level 5 --env surface > /tmp/shank_relay_client.log 2>&1; \
	  CLIENT_STATUS=$$?; wait $$HOST_PID; HOST_STATUS=$$?; \
	  cat /tmp/shank_relay_host.log; cat /tmp/shank_relay_client.log; \
	  grep -q 'level=5 env=0 mods=3' /tmp/shank_relay_client.log && \
	  test $$CLIENT_STATUS -eq 0 -a $$HOST_STATUS -eq 0
