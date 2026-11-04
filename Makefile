# 🛹 SHANKPIT Master Makefile (CI-safe, deterministic outputs)

# ---- Tooling ----
CC       := gcc
BIN_DIR  := bin

# ---- Flags ----
CFLAGS   := -O2 -Wall -D_REENTRANT
INCLUDES := -Ipackages/common -Ipackages/simulation -Ipackages/render -Ipackages/world -Ipackages/goldenband -Ipackages/reflux

LIBS_GL  := -lSDL2 -lGL -lGLU -lm
LIBS_M   := -lm

# ---- Sources ----
EDU_LOBBY_SRC := packages/education/edu_lexer.c packages/education/edu_parser.c packages/education/edu_bytecode.c packages/education/edu_vm.c packages/education/edu_bindings.c packages/education/edu_script.c packages/education/eduvm_host.c packages/education/eduvm_snippets.c packages/education/eduvm_mod_unit.c
LOBBY_SRC    := apps/lobby/src/main.c packages/common/units_rules.c packages/protocol/wow_wire_rules.c apps/lobby/src/editor_widget_bridge.c packages/simulation/story_ai.c packages/simulation/ai_nav.c packages/simulation/humanness.c packages/simulation/cutscene.c packages/simulation/typing_lesson.c packages/simulation/tyler_coldopen.c packages/simulation/tyler_e03_coldopen.c packages/simulation/tyler_fb01_coldopen.c packages/simulation/tyler_vh02_coldopen.c packages/simulation/tyler_vh03_coldopen.c packages/simulation/tyler_vh04_coldopen.c packages/simulation/tyler_voice_mod.c packages/simulation/day_night_clock.c packages/simulation/world_rules.c packages/simulation/witness_ai.c packages/simulation/zombie_brain.c packages/simulation/zombie_brain_rules.c packages/simulation/witness_sim.c packages/simulation/witness_rules.c packages/simulation/npc_archetype.c packages/simulation/zombie_values.c packages/simulation/avian_values.c packages/simulation/ai_brain_rules.c packages/simulation/world_alerts_mod.c packages/simulation/world_alert_bridge.c packages/simulation/zombies_awareness_rules.c packages/simulation/zombies_hud_bridge.c packages/simulation/zombies_pheromone_rules.c packages/simulation/hammer_tower.c packages/simulation/zombie_taxonomy.c packages/simulation/bioslurry.c packages/simulation/citizen_life.c packages/simulation/citizen_avatar.c packages/simulation/citizen_avatar_rules.c packages/simulation/dw_play.c packages/simulation/dw_engine.c packages/simulation/dw_match.c packages/simulation/dw_ladder.c packages/simulation/dw_stage.c packages/simulation/dw_stage_rules.c packages/simulation/dw_net.c packages/simulation/dw_rules.c packages/simulation/dw_citizen_rules.c packages/simulation/dw_rating.c packages/simulation/food_pickup.c packages/simulation/lab_station_host.c packages/simulation/lab_station_rules.c packages/simulation/shadow_war_host.c packages/simulation/shadow_war_rules.c packages/simulation/gun_items.c packages/simulation/chests.c packages/simulation/chest_rules.c packages/simulation/shield_packs.c packages/simulation/giant_bug_values.c packages/simulation/giant_bug_brain.c packages/render/proc_tex.c packages/render/retro_material.c packages/render/retro_sky.c packages/render/sky_weather.c packages/render/retro_lighting.c packages/render/env_matrix.c packages/render/gl_shader.c packages/render/bloom.c packages/world/terrain.c packages/world/parena_runtime.c packages/world/png_decode_gen.c packages/ui/menu_gen.c packages/pellegrino_embed/os_menu_embed.c packages/audio/audio.c packages/audio/audio_wav.c packages/audio/audio_chain.c packages/audio/audio_dsp_gen.c packages/goldenband/gband.c packages/goldenband/gskel.c packages/goldenband/gmesh.c packages/goldenband/gband_mesh_rig.c packages/goldenband/gseq.c packages/goldenband/gpose.c packages/goldenband/gsync.c packages/goldenband/gband_skel_npc.c packages/reflux/reflux_runtime.c packages/reflux/reflux_mod.c packages/simulation/brick_mod.c packages/simulation/brick_bodies.c packages/simulation/ambient_rules.c packages/simulation/lighting_rules.c packages/simulation/material_rules.c packages/simulation/structure_rules.c packages/simulation/garage_rules.c packages/simulation/buggy_rules.c packages/simulation/heli_rules.c packages/simulation/rocket_car_rules.c packages/simulation/drone_rules.c packages/simulation/airplane_rules.c packages/simulation/vehicle_salvage_rules.c packages/simulation/vehicle_burn_rules.c packages/simulation/br_stadium.c packages/simulation/anim_rules.c packages/simulation/slide_rules.c packages/simulation/empire_rules.c packages/simulation/empire_units_rules.c packages/render/camera_rules.c packages/render/filter_rules.c packages/simulation/ragdoll_pool.c packages/simulation/rigid_ragdoll.c packages/simulation/ragdoll_rules.c packages/goldenband/grb.c packages/simulation/mission.c packages/simulation/party_rules.c packages/simulation/chat_rules.c packages/simulation/hoverboard_rules.c packages/simulation/widget_rules.c packages/simulation/zone_rules.c packages/simulation/coop_host.c packages/simulation/zone_beacon.c $(EDU_LOBBY_SRC)
SERVER_SRC   := apps/server/src/main.c packages/protocol/wow_wire_rules.c packages/common/units_rules.c packages/simulation/shield_packs.c packages/simulation/story_ai.c packages/simulation/ai_nav.c packages/simulation/humanness.c packages/simulation/tyler_coldopen.c packages/simulation/tyler_e03_coldopen.c packages/simulation/tyler_fb01_coldopen.c packages/simulation/tyler_vh02_coldopen.c packages/simulation/tyler_vh03_coldopen.c packages/simulation/tyler_vh04_coldopen.c packages/simulation/tyler_voice_mod.c packages/simulation/day_night_clock.c packages/simulation/world_rules.c packages/simulation/witness_ai.c packages/simulation/zombie_brain.c packages/simulation/zombie_brain_rules.c packages/simulation/witness_sim.c packages/simulation/witness_rules.c packages/simulation/npc_archetype.c packages/simulation/zombie_values.c packages/simulation/avian_values.c packages/simulation/ai_brain_rules.c packages/simulation/giant_bug_values.c packages/simulation/giant_bug_brain.c packages/simulation/zombies_awareness_rules.c packages/simulation/zombies_hud_bridge.c packages/simulation/zombies_pheromone_rules.c packages/simulation/hammer_tower.c packages/simulation/zombie_taxonomy.c packages/simulation/bioslurry.c packages/simulation/citizen_life.c packages/simulation/citizen_avatar.c packages/simulation/citizen_avatar_rules.c packages/simulation/dw_engine.c packages/simulation/dw_match.c packages/simulation/dw_ladder.c packages/simulation/dw_stage.c packages/simulation/dw_stage_rules.c packages/simulation/dw_net.c packages/simulation/dw_rules.c packages/simulation/dw_citizen_rules.c packages/simulation/dw_rating.c packages/world/terrain.c packages/reflux/reflux_runtime.c packages/reflux/reflux_mod.c packages/simulation/brick_mod.c packages/simulation/brick_bodies.c packages/simulation/structure_rules.c packages/world/parena_runtime.c packages/goldenband/grb.c packages/simulation/vehicle_salvage_rules.c packages/simulation/garage_rules.c packages/simulation/buggy_rules.c packages/simulation/heli_rules.c packages/simulation/rocket_car_rules.c packages/simulation/drone_rules.c packages/simulation/airplane_rules.c packages/simulation/br_stadium.c packages/simulation/zone_rules.c packages/simulation/widget_rules.c packages/simulation/empire_units_rules.c packages/simulation/zone_beacon.c packages/simulation/coop_host.c
SERVERCTL_SRC:= apps/server/serverctl.c

# ---- Source-list export for CI ----
# CI (.github/workflows/{tests,release}.yml) compiles the Windows lobby and the Linux server with
# raw gcc/mingw lines instead of `make` (different libs/flags). Those lines used to carry a
# hand-copied copy of LOBBY_SRC/SERVER_SRC and drifted NINE times -- including a 3-day red release
# pipeline (2026-09-28..10-01) -- so they now expand these two targets instead:
#   gcc $(make -s print-server-src) ...        x86_64-w64-mingw32-gcc $(make -s print-lobby-src) ...
# A new packages/*.c added to LOBBY_SRC/SERVER_SRC above is now picked up by CI automatically.
.PHONY: print-lobby-src print-server-src test-os-menu test-os-menu-gl check-windows

# Compile every Windows-client source under mingw-w64 (no link, so no .a/.dll objects needed). Catches
# compile-only breakage that otherwise waits for a release job -- see the script's own header comment.
check-windows:
	bash scripts/check_windows_build.sh
print-lobby-src:
	@echo $(LOBBY_SRC)
print-server-src:
	@echo $(SERVER_SRC)

# ---- Outputs ----
LOBBY_BIN    := $(BIN_DIR)/shank_lobby
SERVER_BIN   := $(BIN_DIR)/shank_server
SERVERCTL_BIN:= $(BIN_DIR)/serverctl
GO_SERVER_BIN := $(BIN_DIR)/shank_go_server
EMILY_BOT_BIN := $(BIN_DIR)/emily-bot
EA_DIR       := dist/ea

# ---- Targets ----
.PHONY: test-witness-ai-zombies test-bullet-hole test-queue-level test-baked-levels test-level-save bake-levels all lobby server serverctl clean setup print go-server ea ea-windows emily-bot rigid-ragdoll test-physics test-audio-chain ux-screenshot-test

all: $(LOBBY_BIN) $(SERVER_BIN)

# Ensure bin/ exists even when building a single target
$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

setup: $(BIN_DIR)

# ---- EDITOR.GAME in-process widget (S559, 2026-09-27) ----
# Founder real-time: "the editor app fails to launch -- instead of having it launch it should pop
# a widget up on the screen ... deeply integrate it as a widget on the screen the notes auto
# save." apps/lobby/src/editor_widget_bridge.c links against EDITOR.GAME's own real
# editor_widget_* API, consumed as a sibling checkout (../EDITOR.GAME -- same path MODULE.bazel's
# own local_path_override already assumes). Same 5-file build EDITOR.GAME's own plain Makefile
# uses to produce its standalone `editor-game` binary, just linked into shank_lobby instead:
# gen/editor_full.c (the generated PARENA stdlib + examples/editor_main.c, concatenated -- see
# EDITOR.GAME/Makefile's own identical rule) and runtime/parena_runtime.c define/call the plain
# arena_init/arena_alloc/arena_strdup/arena_free_all names, which would otherwise collide at link
# time with SHANKPIT's own already-linked, deliberately-minimal packages/world/parena_runtime.c
# (same 4 names, real but different, narrower implementation) -- both files are compiled here with
# a matching -D rename so ONLY this pair's calls/definitions move to edg_arena_*, leaving
# SHANKPIT's own copy completely untouched. src/arena.c/src/fmt.c/runtime/prnfmt_bridge.c are
# EDITOR.GAME's own SEPARATE compiler-internal bump allocator, already renamed to pf_arena_* by
# EDITOR.GAME's own PRNFMT_RENAME convention (unrelated to the edg_arena_* rename above -- two
# different collisions, two different renames, matching EDITOR.GAME's own Makefile/BUILD.bazel
# precedent exactly).
EDITOR_GAME_DIR    := ../EDITOR.GAME
# vec_i32_at -- PARENA's C emitter generates this Vec<i32> helper as a plain global (not static)
# in every translation unit that uses Vec<i32>; packages/world/png_decode_gen.c (SHANKPIT's own
# PARENA-generated code, already in LOBBY_SRC) happens to generate one too, so it collides the
# same way arena_init did -- confirmed via `nm -g` on both object files, exactly one overlap.
EDITOR_GAME_RENAME := -Darena_init=edg_arena_init -Darena_alloc=edg_arena_alloc -Darena_strdup=edg_arena_strdup -Darena_free_all=edg_arena_free_all -Dvec_i32_at=edg_vec_i32_at
EDITOR_GAME_PRNFMT := -Darena_init=pf_arena_init -Darena_alloc=pf_arena_alloc -Darena_strdup=pf_arena_strdup -Darena_free_all=pf_arena_free_all
EDITOR_GAME_OBJS   := $(BIN_DIR)/editor_full.o $(BIN_DIR)/editor_runtime.o $(BIN_DIR)/editor_pf_arena.o $(BIN_DIR)/editor_pf_fmt.o $(BIN_DIR)/editor_pf_bridge.o

$(EDITOR_GAME_DIR)/gen/editor_full.c: $(EDITOR_GAME_DIR)/gen/editor_stdlib_gen.c $(EDITOR_GAME_DIR)/examples/editor_main.c
	cat $(EDITOR_GAME_DIR)/gen/editor_stdlib_gen.c $(EDITOR_GAME_DIR)/examples/editor_main.c > $@

$(BIN_DIR)/editor_full.o: $(EDITOR_GAME_DIR)/gen/editor_full.c | $(BIN_DIR)
	$(CC) -std=c99 -w -DEDITOR_WIDGET_TEST_BUILD $(EDITOR_GAME_RENAME) -I$(EDITOR_GAME_DIR)/runtime -c $< -o $@

$(BIN_DIR)/editor_runtime.o: $(EDITOR_GAME_DIR)/runtime/parena_runtime.c | $(BIN_DIR)
	$(CC) -std=c99 -w $(EDITOR_GAME_RENAME) -I$(EDITOR_GAME_DIR)/runtime -c $< -o $@

$(BIN_DIR)/editor_pf_arena.o: $(EDITOR_GAME_DIR)/src/arena.c | $(BIN_DIR)
	$(CC) -std=c99 -w $(EDITOR_GAME_PRNFMT) -c $< -o $@

$(BIN_DIR)/editor_pf_fmt.o: $(EDITOR_GAME_DIR)/src/fmt.c | $(BIN_DIR)
	$(CC) -std=c99 -w $(EDITOR_GAME_PRNFMT) -c $< -o $@

$(BIN_DIR)/editor_pf_bridge.o: $(EDITOR_GAME_DIR)/runtime/prnfmt_bridge.c | $(BIN_DIR)
	$(CC) -std=c99 -w $(EDITOR_GAME_PRNFMT) -I$(EDITOR_GAME_DIR)/runtime -I$(EDITOR_GAME_DIR)/src -c $< -o $@

# Windows (mingw) variants of the same 5 objects, for ea-windows below -- separate output
# filenames (.win.o) so a native `make lobby` and `make ea-windows` in the same tree never clobber
# each other's objects. Verified for real: EDITOR.GAME's own runtime/parena_runtime.c and
# generated stdlib code cross-compile under x86_64-w64-mingw32-gcc with no source changes at all
# (checked directly, not assumed -- the only NEW cross-build requirement is SDL2_ttf's own mingw
# devel kit, same one PITVIPER/IDUNA.GAME/EDITOR.GAME's own standalone Windows builds already need,
# per SDL2_MINGW_PREFIX below).
MINGW_CC := x86_64-w64-mingw32-gcc
EDITOR_GAME_OBJS_WIN := $(BIN_DIR)/editor_full.win.o $(BIN_DIR)/editor_runtime.win.o $(BIN_DIR)/editor_pf_arena.win.o $(BIN_DIR)/editor_pf_fmt.win.o $(BIN_DIR)/editor_pf_bridge.win.o

$(BIN_DIR)/editor_full.win.o: $(EDITOR_GAME_DIR)/gen/editor_full.c | $(BIN_DIR)
	$(MINGW_CC) -std=c99 -w -DEDITOR_WIDGET_TEST_BUILD $(EDITOR_GAME_RENAME) -I$(EDITOR_GAME_DIR)/runtime -I$(SDL2_MINGW_PREFIX)/include -c $< -o $@

$(BIN_DIR)/editor_runtime.win.o: $(EDITOR_GAME_DIR)/runtime/parena_runtime.c | $(BIN_DIR)
	$(MINGW_CC) -std=c99 -w $(EDITOR_GAME_RENAME) -I$(EDITOR_GAME_DIR)/runtime -I$(SDL2_MINGW_PREFIX)/include -c $< -o $@

$(BIN_DIR)/editor_pf_arena.win.o: $(EDITOR_GAME_DIR)/src/arena.c | $(BIN_DIR)
	$(MINGW_CC) -std=c99 -w $(EDITOR_GAME_PRNFMT) -c $< -o $@

$(BIN_DIR)/editor_pf_fmt.win.o: $(EDITOR_GAME_DIR)/src/fmt.c | $(BIN_DIR)
	$(MINGW_CC) -std=c99 -w $(EDITOR_GAME_PRNFMT) -c $< -o $@

$(BIN_DIR)/editor_pf_bridge.win.o: $(EDITOR_GAME_DIR)/runtime/prnfmt_bridge.c | $(BIN_DIR)
	$(MINGW_CC) -std=c99 -w $(EDITOR_GAME_PRNFMT) -I$(EDITOR_GAME_DIR)/runtime -I$(EDITOR_GAME_DIR)/src -I$(SDL2_MINGW_PREFIX)/include -c $< -o $@

# ---- PELLEGRINO in-phone browser (SHANKPIT-PELLEGRINO-APP-02) ----
# Pre-built objects linked at the end (NOT in LOBBY_SRC): needs its own mbedTLS copts/includes that
# the rest of the lobby must never see. mbedTLS source comes from ../PELLEGRINO's pinned fetch script.
PGB_DIR     := packages/pellegrino_browser_embed
PELLEGRINO_DIR ?= ../PELLEGRINO
PGB_MBEDTLS  = $(shell [ -d $(PELLEGRINO_DIR) ] && cd $(PELLEGRINO_DIR) && bash scripts/fetch_mbedtls.sh 2>/dev/null)
PGB_DEFS    := -DPARENA_WITH_TLS -DPARENA_TLS_STATIC_HEAP -DPARENA_TLS_HEAP_STATS -DPARENA_WITH_QUIC_CRYPTO "-DMBEDTLS_USER_CONFIG_FILE=\"pellegrino_user_config.h\""
PGB_OBJ_DIR := $(BIN_DIR)/pgb
# A box without PELLEGRINO has no mbedtls TREE to compile, but the system package provides the same
# three libraries -- without this the lobby dies on ~40 undefined hex/tls/x509 symbols that are nobody's bug.
PGB_MBED_LIBS := $(if $(wildcard $(PGB_MBEDTLS)/library/*.c),,$(shell pkg-config --libs mbedtls mbedx509 mbedcrypto 2>/dev/null))
PGB_MBED_OBJS = $(patsubst %.c,$(PGB_OBJ_DIR)/mbed_%.o,$(notdir $(wildcard $(PGB_MBEDTLS)/library/*.c)))
PGB_OBJS     = $(PGB_OBJ_DIR)/phone_browser_embed.o $(PGB_OBJ_DIR)/monocypher.o $(PGB_MBED_OBJS)

$(PGB_OBJ_DIR):
	mkdir -p $@

$(PGB_OBJ_DIR)/phone_browser_embed.o: $(PGB_DIR)/phone_browser_embed.c $(PGB_DIR)/pellegrino_gen.inc | $(PGB_OBJ_DIR)
	$(CC) -std=gnu99 -O2 -w $(PGB_DEFS) -I$(PGB_DIR) -I$(PGB_DIR)/runtime -I$(PGB_MBEDTLS)/include -c $< -o $@

$(PGB_OBJ_DIR)/monocypher.o: $(PGB_DIR)/runtime/aead/monocypher.c | $(PGB_OBJ_DIR)
	$(CC) -std=gnu99 -O2 -w -c $< -o $@

$(PGB_OBJ_DIR)/mbed_%.o: | $(PGB_OBJ_DIR)
	$(CC) -std=gnu99 -O2 -w $(PGB_DEFS) -I$(PGB_DIR)/runtime -I$(PGB_MBEDTLS)/include -I$(PGB_MBEDTLS)/library -c $(PGB_MBEDTLS)/library/$*.c -o $@

# ---- CLIENT / LOBBY ----
lobby: $(LOBBY_BIN)

$(LOBBY_BIN): $(LOBBY_SRC) $(EDITOR_GAME_OBJS) $(PGB_OBJS) | $(BIN_DIR)
	@echo "🔨 Building Lobby Client..."
	$(CC) $(CFLAGS) $(INCLUDES) $(LOBBY_SRC) $(EDITOR_GAME_OBJS) $(PGB_OBJS) -o $@ $(LIBS_GL) -lSDL2_ttf $(PGB_MBED_LIBS)

# ---- OS menu through PELLEGRINO (kanban #627) ----
# Headless (SDL_VIDEODRIVER=dummy): builds the HTML menu, paints it with PELLEGRINO, clicks every tile, checks the
# index mapping and keyboard activation. Links packages/world/parena_runtime.c too, to prove the pgm_ rename
# (packages/pellegrino_embed/pg_rename.h) keeps the engine's runtime from colliding with the lobby's.
test-os-menu: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -w -Ipackages/pellegrino_embed -Ipackages/world packages/pellegrino_embed/os_menu_embed.c packages/pellegrino_embed/os_menu_embed_test.c packages/world/parena_runtime.c -o $(BIN_DIR)/os_menu_embed_test -lSDL2 -lm
	SDL_VIDEODRIVER=dummy $(BIN_DIR)/os_menu_embed_test

# Same menu through the lobby's real GL path (packages/pellegrino_embed/os_menu_gl.h) in a real GL context:
# needs a display (xvfb-run make test-os-menu-gl) and libGL; checks tile colours at screen positions + hit mapping.
test-os-menu-gl: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -w -Ipackages/pellegrino_embed -Ipackages/world packages/pellegrino_embed/os_menu_embed.c packages/pellegrino_embed/os_menu_gl_test.c packages/world/parena_runtime.c -o $(BIN_DIR)/os_menu_gl_test -lSDL2 -lGL -lm
	$(BIN_DIR)/os_menu_gl_test

# ---- GAME SERVER ----
server: $(SERVER_BIN)

$(SERVER_BIN): $(SERVER_SRC) | $(BIN_DIR)
	@echo "🔨 Building Game Server..."
	$(CC) $(CFLAGS) $(INCLUDES) $(SERVER_SRC) -o $@ $(LIBS_M)

# ---- RIGID BODY PHYSICS (GOLDEN BAND grb, vendored in packages/goldenband) ----
# Founder real-time 2026-09-27: "upgrade shankpit ... to formal rigid body physics".
PHYSICS_SRC := packages/simulation/rigid_ragdoll.c packages/goldenband/grb.c packages/goldenband/gskel.c packages/goldenband/gpose.c

rigid-ragdoll: $(BIN_DIR)/rigid_ragdoll

$(BIN_DIR)/rigid_ragdoll: tools/rigid_ragdoll/main.c $(PHYSICS_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -Wextra $(INCLUDES) $^ -o $@ $(LIBS_M)

# Headless physics tests (run from the repo root -- they load assets/goldenband/*.gskel).
test-physics: | $(BIN_DIR)
	$(CC) $(CFLAGS) -Wextra $(INCLUDES) packages/simulation/rigid_ragdoll_test.c $(PHYSICS_SRC) -o $(BIN_DIR)/rigid_ragdoll_test $(LIBS_M)
	./$(BIN_DIR)/rigid_ragdoll_test

# Real, live UX regression test -- boots the real lobby binary under headless Xvfb, opens the
# real LEVELS overlay via a synthetic XTEST Enter keypress, fails if either screen renders
# suspiciously close to solid black (the blank-screen symptom this was written to catch). Needs
# lobby already built (make lobby); needs Xvfb/ImageMagick/python3-xlib installed.
ux-screenshot-test: lobby
	./scripts/ux_screenshot_test.sh

# ---- SERVER CONTROL (OPTIONAL, LOCAL ONLY) ----
serverctl: $(SERVERCTL_BIN)

$(SERVERCTL_BIN): $(SERVERCTL_SRC) | $(BIN_DIR)
	@echo "🖥️ Building Server Control (requires ncurses)..."
	$(CC) -O2 $< -o $@ -lncurses

# ---- GO MATCHMAKER / SCENE SERVER ----
go-server: $(GO_SERVER_BIN)

$(GO_SERVER_BIN): $(BIN_DIR)
	@echo "🔨 Building Go scene server..."
	GOWORK=off go build -o $(GO_SERVER_BIN) ./apps2/server-go/

# ---- EMILY BOT (Go headless player) ----
emily-bot: $(EMILY_BOT_BIN)

$(EMILY_BOT_BIN): $(BIN_DIR)
	@echo "Building Emily bot client..."
	GOWORK=off go build -o $(EMILY_BOT_BIN) ./apps2/emily-bot/

# ---- STEAM EA BUILD (Linux) ----
# Packages: headless Go scene server + C lobby client + README into dist/ea/
# Prerequisites: SDL2 + OpenGL development libraries installed
ea: $(LOBBY_BIN) $(GO_SERVER_BIN)
	@echo "📦 Packaging EA build (Linux)..."
	@mkdir -p $(EA_DIR)
	@cp $(LOBBY_BIN)  $(EA_DIR)/shank_lobby
	@cp $(GO_SERVER_BIN) $(EA_DIR)/shank_go_server
	@cp docs/EA_BUILD.md $(EA_DIR)/README.txt
	@echo "✅ EA build ready at $(EA_DIR)/"
	@ls -lh $(EA_DIR)/

# ---- STEAM EA BUILD (Windows cross-compile, requires mingw-w64) ----
# Real, found-live fix (S459-41 follow-up): this target used to target i686-w64-mingw32-gcc, but
# the only real mingw SDL2 dev kit checked out on this box (/tmp/sdl2_mingw) is x86_64, matching
# the Go server's own GOARCH=amd64 above -- 32-bit and 64-bit mingw libs are never interchangeable
# (verified live: linking against the x86_64 archive with the i686 compiler leaves real symbols
# like SDL_GetError undefined, since the .o members are a different COFF machine type entirely).
# SDL2_MINGW_PREFIX is overridable for a box with its dev kit somewhere else.
SDL2_MINGW_PREFIX ?= /tmp/sdl2_mingw
ea-windows: $(BIN_DIR) $(EDITOR_GAME_OBJS_WIN)
	@echo "🪟 Cross-compiling Go server for Windows..."
	GOWORK=off GOOS=windows GOARCH=amd64 go build -o $(BIN_DIR)/shank_go_server.exe ./apps2/server-go/
	@echo "🔨 Cross-compiling C client for Windows (requires x86_64-w64-mingw32-gcc)..."
	# -lSDL2_ttf (S559, EDITOR.GAME in-process widget) needs SDL2_ttf's own mingw devel kit merged
	# into $(SDL2_MINGW_PREFIX) -- same one PITVIPER/IDUNA.GAME/EDITOR.GAME's own standalone
	# Windows builds already fetch (see .github/workflows/release.yml's own "Install Dependencies"
	# step); the base SDL2 mingw devel kit alone does not ship it.
	# Real in-phone PELLEGRINO browser if scripts/build_pgb_windows.sh succeeds (needs ../PELLEGRINO), else the stub.
	-bash scripts/build_pgb_windows.sh $(SDL2_MINGW_PREFIX)
	x86_64-w64-mingw32-gcc $(CFLAGS) $(INCLUDES) -I$(SDL2_MINGW_PREFIX)/include $(LOBBY_SRC) $(EDITOR_GAME_OBJS_WIN) \
		$(if $(wildcard $(BIN_DIR)/pgb_win/phone_browser_embed.o),$(BIN_DIR)/pgb_win/phone_browser_embed.o $(BIN_DIR)/pgb_win/monocypher.o $(wildcard $(BIN_DIR)/pgb_win/mbed/*.o) -lbcrypt,packages/pellegrino_browser_embed/phone_browser_stub.c) \
		-o $(BIN_DIR)/shank_lobby.exe \
		-L$(SDL2_MINGW_PREFIX)/lib -lSDL2 -lSDL2_ttf -lopengl32 -lglu32 -lm -static-libgcc \
		-lws2_32 -ldinput8 -ldxguid -ldxerr8 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lsetupapi -lversion -luuid
	@echo "📦 Packaging EA build (Windows)..."
	@mkdir -p dist/ea-windows
	@cp $(BIN_DIR)/shank_go_server.exe dist/ea-windows/
	@cp $(BIN_DIR)/shank_lobby.exe dist/ea-windows/
	@[ -f $(BIN_DIR)/pgb_win/cacert.pem ] && cp $(BIN_DIR)/pgb_win/cacert.pem dist/ea-windows/ || true
	@cp docs/EA_BUILD.md dist/ea-windows/README.txt
	@echo "✅ Windows EA build ready at dist/ea-windows/"

clean:
	@echo "🧹 Cleaning..."
	rm -rf $(BIN_DIR) dist/

print:
	@echo "LOBBY_BIN=$(LOBBY_BIN)"
	@echo "SERVER_BIN=$(SERVER_BIN)"
	@echo "GO_SERVER_BIN=$(GO_SERVER_BIN)"
	@echo "EA_DIR=$(EA_DIR)"

# test-bullet-hole -- per-gun bullet-hole decals (2026-10-01): shot detection, slot/size/ray mapping,
# ring buffer, and that the 4 NOCK-exported PNGs (PARENA-authored, ImageMagick-paletted) decode through
# PARENA's own png_decode. No SDL/GL needed.
test-bullet-hole:
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Ipackages/world -o /tmp/bullet_hole_test packages/world/bullet_hole_test.c \
		packages/world/png_decode_gen.c packages/world/parena_runtime.c -lm
	/tmp/bullet_hole_test

# test-iduna-login -- the vendored IDUNA SSO header's own loopback round trip, driven by a scripted
# browser over real sockets. No IDUNA, no SDL/GL.
test-iduna-login:
	$(CC) -std=gnu99 -O2 -Wall -Wextra -pedantic -Werror -o /tmp/iduna_login_test packages/iduna_login/iduna_login_test.c -pthread
	/tmp/iduna_login_test

# test-iduna-live -- the same header against a real IDUNA (IDUNA_LIVE_BASE=http://127.0.0.1:18080):
# registers a throwaway account, signs in the way the SSO page's own JS does, fetches the page at the
# URL the header built, then hands that real JWT to the loopback listener. Skips when unset.
test-iduna-live:
	$(CC) -std=gnu99 -O2 -Wall -Wextra -pedantic -Werror -o /tmp/iduna_login_live_test packages/iduna_login/iduna_login_live_test.c
	/tmp/iduna_login_live_test

# test-queue-level -- PACKET_QUEUE_LEVEL wire contract (card #481): size, id round-trip incl. -1, unique type.
test-spraypaint:
	$(CC) -std=c99 -O2 -Wall -Wextra -o /tmp/spraypaint_test packages/world/spraypaint_test.c -lm
	/tmp/spraypaint_test

test-spray-decal:
	$(CC) -std=c99 -O2 -Wall -Wextra -pedantic -Werror -Ipackages/common -o /tmp/spray_decal_test packages/common/spray_decal_test.c
	/tmp/spray_decal_test

test-edu-widgets:
	$(CC) -O1 -Wall -Wextra -Ipackages/education packages/education/edu_widgets_test.c packages/education/edu_lexer.c packages/education/edu_bytecode.c packages/education/edu_parser.c packages/education/edu_vm.c packages/education/edu_bindings.c -o $(BIN_DIR)/edu_widgets_test
	$(BIN_DIR)/edu_widgets_test
	$(CC) -O1 -Wall -Wextra packages/world/level_boxes_edu_widgets_test.c -o $(BIN_DIR)/level_boxes_edu_widgets_test
	$(BIN_DIR)/level_boxes_edu_widgets_test

test-queue-level:
	$(CC) -std=c99 -O2 -Wall -Wextra -pedantic -Werror -Ipackages/common -o /tmp/queue_level_test packages/common/queue_level_test.c
	/tmp/queue_level_test

# test-shield-packs -- card #541: 25% drop rule, once-per-death latch, pickup reach, wire contract of PACKET_SHIELD_PACKS.
test-shield-packs:
	$(CC) -std=c99 -O2 -Wall -Wextra -pedantic -Werror -Ipackages/common -o /tmp/shield_packs_test packages/simulation/shield_packs_test.c packages/simulation/shield_packs.c -lm
	/tmp/shield_packs_test

# test-lofts -- the recursive lofts tree (levels/lofts/): every object reference resolves, nothing
# cycles, the eight buildings flatten to genuinely destructible geometry under the same rule the
# engine applies (brick_world_box_is_slab), and flattening into var/zombie/nextown_zombies.json is
# idempotent and stays inside LEVEL_BOXES_MAX. No SDL/GL needed.
.PHONY: test-lofts
build-lofts:
	python3 tools/build_loft_city.py

test-lofts:
	python3 tools/build_loft_city.py --check

# bake-levels -- card #479: regenerate packages/world/baked_levels_gen.h from a live IDUNA (review + commit the diff).
bake-levels:
	python3 scripts/bake_levels.py

# test-level-save -- EDIT MAP's F6 writes a complete level JSON with no IDUNA: every parsed field
# (doors, buttons, nav nodes, characters, exits, zones, edu widgets, lab stations, buggy spawns,
# brick damage) must survive save -> load.
test-level-save:
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -Ipackages/world -o /tmp/level_save_test packages/world/level_save_test.c -lm
	/tmp/level_save_test

# test-baked-levels -- card #479/#480: the baked level table serves the registry + every export offline.
test-baked-levels:
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -Ipackages/world -o /tmp/baked_levels_test packages/world/baked_levels_test.c -lm
	/tmp/baked_levels_test

# test-audio-chain -- NOCK filter chains in the engine (2026-09-27): packages/audio/audio_chain.c
# over PARENA-generated audio_dsp_gen.c must reproduce IDUNA NOCK's TypeScript render bit-for-bit
# (tests/audio/nock_chain_vectors.txt). No SDL needed.
test-audio-chain:
	$(CC) -std=c99 -O2 -Wall -Wextra -pedantic -Werror -Wno-unused-parameter tests/audio/test_audio_chain.c \
		packages/audio/audio_chain.c packages/audio/audio_dsp_gen.c -o /tmp/test_audio_chain -lm
	/tmp/test_audio_chain tests/audio/nock_chain_vectors.txt

# Per-package build fragments (parallel-work convention, 2026-10-01): each work package adds its own
# mk/<pkg>.mk with its own test targets instead of editing this file.
-include mk/*.mk

WAI_TEST_SRC := packages/simulation/witness_ai.c packages/simulation/zombie_brain.c packages/simulation/zombie_brain_rules.c packages/simulation/witness_sim.c packages/simulation/witness_rules.c packages/simulation/npc_archetype.c packages/simulation/zombie_values.c packages/simulation/avian_values.c packages/simulation/ai_brain_rules.c packages/simulation/giant_bug_values.c packages/simulation/giant_bug_brain.c packages/simulation/zombies_pheromone_rules.c packages/simulation/day_night_clock.c packages/simulation/world_rules.c packages/simulation/humanness.c packages/simulation/zombie_taxonomy.c packages/simulation/bioslurry.c packages/simulation/citizen_life.c packages/simulation/citizen_avatar.c packages/simulation/citizen_avatar_rules.c packages/simulation/dw_engine.c packages/simulation/dw_match.c packages/simulation/dw_ladder.c packages/simulation/dw_stage.c packages/simulation/dw_stage_rules.c packages/simulation/dw_net.c packages/simulation/dw_rules.c packages/simulation/dw_citizen_rules.c packages/simulation/dw_rating.c packages/reflux/reflux_runtime.c packages/reflux/reflux_mod.c
test-witness-ai-zombies: | $(BIN_DIR)
	$(CC) -g -O1 -fsanitize=address,undefined -Wall $(INCLUDES) packages/simulation/witness_ai_zombies_test.c $(WAI_TEST_SRC) -o $(BIN_DIR)/witness_ai_zombies_test $(LIBS_M)
	./$(BIN_DIR)/witness_ai_zombies_test

# The player's KIT (packages/common/kit.h): 14 bind slots (1-9, 0, `, -, =, C), three prefab kits,
# cycling, the builder's draft, and the three HTML+CSS pages the phone renders through PELLEGRINO.
.PHONY: test-kit
test-kit: | $(BIN_DIR)
	$(CC) -std=c99 -Wall -Wextra -Werror -Ipackages/common packages/common/kit_test.c -o $(BIN_DIR)/kit_test
	./$(BIN_DIR)/kit_test

# The kit's three VEHICLE gadgets (packages/simulation/vehicle_call.h) and the rocket car's profile
# (physics.h simulate_rocket_car_state, numbers from PARENA/stdlib/shankpit/rocket_car_rules.prn).
.PHONY: test-vehicle-call
test-vehicle-call: | $(BIN_DIR)
	$(CC) -g -O1 -fsanitize=address,undefined -Wall $(INCLUDES) packages/simulation/vehicle_call_test.c $(filter-out apps/server/src/main.c,$(SERVER_SRC)) -o $(BIN_DIR)/vehicle_call_test $(LIBS_M)
	./$(BIN_DIR)/vehicle_call_test

# MATERIAL DRAW PATH (packages/render/material_draw.h): which pass a level material's shader selects,
# and the PARENA reflection strengths the mirror path depends on
.PHONY: test-material-draw
test-material-draw: | $(BIN_DIR)
	$(CC) -g -O1 -fsanitize=address,undefined -Wall -Wextra -Werror $(INCLUDES) packages/render/material_draw_test.c packages/simulation/material_rules.c packages/world/parena_runtime.c -o $(BIN_DIR)/material_draw_test $(LIBS_M)
	./$(BIN_DIR)/material_draw_test

# GROUND FOG (packages/render/ground_fog.h): weather fog that pools ON THE GROUND -- a height term
# layered over the existing distance fog -- lit by the same unified environment matrix the rest of the
# lighting reads (occlusion, scattered bleed, exposure, and a lightning strike that whitens as well as
# brightens). Header-only and GL-free, so the exact model the lobby draws with is exercised with no GPU.
.PHONY: test-ground-fog
# LEVEL SUBTRACT MODIFIER (packages/world/level_boxes.h): an authored cube that cuts itself out of
# whatever level geometry it intersects -- elevator shafts threaded through floors, stairwells punched
# through decks. Exact AABB-minus-AABB at load time, so every new face is axis-aligned on the cut's own
# plane (the same geometry the shotgun leaves, without the cell grid's 3-unit raggedness).
.PHONY: test-level-subtract
test-level-subtract: | $(BIN_DIR)
	$(CC) -g -O1 -fsanitize=address,undefined -Wall -Wextra -Werror $(INCLUDES) packages/world/level_subtract_test.c -o $(BIN_DIR)/level_subtract_test $(LIBS_M)
	./$(BIN_DIR)/level_subtract_test

test-ground-fog: | $(BIN_DIR)
	$(CC) -g -O1 -fsanitize=address,undefined -Wall -Wextra -Werror $(INCLUDES) packages/render/ground_fog_test.c -o $(BIN_DIR)/ground_fog_test $(LIBS_M)
	./$(BIN_DIR)/ground_fog_test

# KIT CALL packet (packages/simulation/kit_call.h + PACKET_KIT_CALL): parsing an untrusted datagram
# and the server's verdict on a real player
.PHONY: test-kit-call
test-kit-call: | $(BIN_DIR)
	$(CC) -g -O1 -fsanitize=address,undefined -Wall $(INCLUDES) packages/simulation/kit_call_test.c $(filter-out apps/server/src/main.c,$(SERVER_SRC)) -o $(BIN_DIR)/kit_call_test $(LIBS_M)
	./$(BIN_DIR)/kit_call_test

# ROCKET CAR rules: PARENA/stdlib/shankpit/rocket_car_rules.prn -> packages/simulation/rocket_car_rules.c packages/simulation/drone_rules.c packages/simulation/airplane_rules.c
.PHONY: regen-rocket-car-rules
regen-rocket-car-rules:
	scripts/gen_rocket_car_rules.sh

# DRONE rules: PARENA/stdlib/shankpit/drone_rules.prn -> packages/simulation/drone_rules.c packages/simulation/airplane_rules.c
.PHONY: regen-drone-rules
regen-drone-rules:
	scripts/gen_drone_rules.sh

# AIRPLANE rules: PARENA/stdlib/shankpit/airplane_rules.prn -> packages/simulation/airplane_rules.c
.PHONY: regen-airplane-rules
regen-airplane-rules:
	scripts/gen_airplane_rules.sh

# VEHICLE BURN rules: PARENA/stdlib/shankpit/vehicle_burn_rules.prn -> packages/simulation/vehicle_burn_rules.c
# (the smoke -> fire -> detonation ladder, and Bedrock Racers' br_fx fire-fx curve it is drawn with)
.PHONY: regen-vehicle-burn-rules
regen-vehicle-burn-rules:
	scripts/gen_vehicle_burn_rules.sh

# The car fire ladder, headless: smoke before flame, a fast swell and a slow fade, one blast at 5 s,
# and crash damage that warns rather than kills.
.PHONY: test-vehicle-burn
test-vehicle-burn: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/simulation -DSHANKPIT_VEHICLE_BURN_PARENA packages/simulation/vehicle_burn_test.c packages/simulation/vehicle_burn_rules.c -o $(BIN_DIR)/vehicle_burn_test $(LIBS_M)
	./$(BIN_DIR)/vehicle_burn_test

# EMPIRE rules: PARENA/stdlib/shankpit/empire_rules.prn -> packages/simulation/empire_rules.c
# (the influence economy and the 72-district sphere geometry behind the phone's EMPIRE app)
.PHONY: regen-empire-rules
regen-empire-rules:
	scripts/gen_empire_rules.sh

# The influence economy and the sphere, headless: costs, the clone premium, the placement gate, the
# squad cap, district alignment/income, and rings that close into a world instead of a funnel.
.PHONY: test-empire-rules
test-empire-rules: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/simulation packages/simulation/empire_rules_test.c packages/simulation/empire_rules.c -o $(BIN_DIR)/empire_rules_test
	./$(BIN_DIR)/empire_rules_test

# WAVE rules: PARENA/stdlib/shankpit/wave_rules.prn -> packages/simulation/wave_rules.c
.PHONY: regen-wave-rules
regen-wave-rules:
	scripts/gen_wave_rules.sh

# WAVE SPAWNER: the runner that turns those numbers into bodies, plus the parity between the generated
# copy and wave_rules.h's defaults. Deliberately NOT added to LOBBY_SRC/SERVER_SRC: story_ai.c uses the
# header's defaults, so a hand-copied source list cannot forget it (that list has drifted nine times).
.PHONY: test-wave-spawner
test-wave-spawner: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/simulation packages/simulation/wave_spawner_test.c packages/simulation/wave_rules.c -o $(BIN_DIR)/wave_spawner_test
	./$(BIN_DIR)/wave_spawner_test

# MATERIAL rules: PARENA/stdlib/shankpit/material_rules.prn -> packages/simulation/material_rules.c
# (UV scroll, frame-loop sheets, palette wrapping, Quake light styles, screen tints, probe-grid axes)
.PHONY: regen-material-rules
regen-material-rules:
	scripts/gen_material_rules.sh

# The storytelling vocabulary, headless: a strobe that actually goes dark, a fluorescent that drops
# out rarely, a broken tube mostly off, scroll/frames that wrap on the second, tints monotonic in the
# danger they represent, palette indices that never go negative, probe axes that clamp.
.PHONY: test-material-rules
test-material-rules: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/simulation packages/simulation/material_rules_test.c packages/simulation/material_rules.c -o $(BIN_DIR)/material_rules_test
	./$(BIN_DIR)/material_rules_test

# CITIZEN AVATARS: PARENA/stdlib/forgedinface/citizen.prn -> packages/simulation/citizen_avatar_rules.c
# (a citizen's body and wardrobe as a total function of the persona citizen_life.h already gave it)
.PHONY: regen-citizen-avatar
regen-citizen-avatar:
	scripts/gen_citizen_avatar.sh

# The generated body, headless. Hand-derived proportions and both clamps, the masc/fem continuum
# reached from both ends, the HSV->RGB wheel anchors, a bench-worker heavier than a raider with the
# shorter legs, the same citizen rebuilt identically from the same seed, and a 256-citizen population
# that does not read as six people.
.PHONY: test-citizen-avatar
test-citizen-avatar: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/simulation packages/simulation/citizen_avatar_test.c packages/simulation/citizen_avatar.c packages/simulation/citizen_avatar_rules.c -o $(BIN_DIR)/citizen_avatar_test $(LIBS_M)
	./$(BIN_DIR)/citizen_avatar_test

# DEADWEIGHT, inside SHANKPIT: the PARENA engine (rules + citizen policy) emitted to C, the
# sequencer that turns it into a match, and the citizens' challenge ladder.
.PHONY: regen-dw-engine
regen-dw-engine:
	scripts/gen_dw_engine.sh

# The whole game, headless: the shim's backend swap and selftest, the rules' base line and triangle,
# a scripted round's exact hull/energy/credit arithmetic, an unsequenced card reported rather than
# faked, a deterministic replay, the citizen policy's deck slices, all four challenge directions, and
# a deck that re-crafts itself when the record and the meta move.
.PHONY: test-deadweight
test-deadweight: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/simulation packages/simulation/dw_test.c packages/simulation/dw_engine.c packages/simulation/dw_match.c packages/simulation/dw_ladder.c packages/simulation/dw_stage.c packages/simulation/dw_stage_rules.c packages/simulation/dw_play.c packages/simulation/dw_net.c packages/simulation/dw_rules.c packages/simulation/dw_citizen_rules.c packages/simulation/dw_rating.c packages/simulation/citizen_life.c -o $(BIN_DIR)/dw_test $(LIBS_M)
	./$(BIN_DIR)/dw_test

# ---- the sequencer's PARITY vectors (the three-way merge, DEADWEIGHT half) ----------------------
# PARENA/stdlib/deadweight/match_parity.txt is asserted by BOTH this test and
# NOOBTOPIA/tests/deadweight_match_test.lua -- the same convention walk_parity.txt set for movement.
# The sequencer itself is the one piece still hand-written per world (dw_match.c here,
# src/deadweight_game.lua there); these rows are what stops the two drifting.
#
# One binary writes AND reads the file: `--dump` is the generator, so the writer and the reader can
# never disagree about the columns. The vectors are the C answers (the C emitted from the .prn is
# authoritative; the Luau port is the bug if it disagrees) -- so after `make regen-dw-parity`, re-check
# a couple of rows BY HAND, or a garbled column would be "parity" with itself.
DW_PARITY_SRC := packages/simulation/dw_match_parity_test.c packages/simulation/dw_engine.c \
                 packages/simulation/dw_match.c packages/simulation/dw_rules.c \
                 packages/simulation/dw_citizen_rules.c
DW_PARITY_TXT ?= ../PARENA/stdlib/deadweight/match_parity.txt

test-dw-parity: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/simulation $(DW_PARITY_SRC) -o $(BIN_DIR)/dw_parity_test $(LIBS_M)
	./$(BIN_DIR)/dw_parity_test

regen-dw-parity: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/simulation $(DW_PARITY_SRC) -o $(BIN_DIR)/dw_parity_test $(LIBS_M)
	./$(BIN_DIR)/dw_parity_test --dump > $(DW_PARITY_TXT)
	@echo "wrote $(DW_PARITY_TXT) -- now: lua ../NOOBTOPIA/tests/deadweight_match_test.lua"

# The LADDER's parity vectors, same discipline as the sequencer's above -- and the fixture carries the
# SCRIPT as well as the outcomes, so both worlds execute the same sequence of ladder operations rather
# than each re-typing it.
DW_LADDER_PARITY_SRC := packages/simulation/dw_ladder_parity_test.c packages/simulation/dw_engine.c \
                        packages/simulation/dw_match.c packages/simulation/dw_ladder.c packages/simulation/dw_stage.c packages/simulation/dw_stage_rules.c \
                        packages/simulation/dw_net.c packages/simulation/citizen_life.c \
                        packages/simulation/dw_rules.c packages/simulation/dw_citizen_rules.c \
                        packages/simulation/dw_rating.c
DW_LADDER_PARITY_TXT ?= ../PARENA/stdlib/deadweight/ladder_parity.txt

test-dw-ladder-parity: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/simulation $(DW_LADDER_PARITY_SRC) -o $(BIN_DIR)/dw_ladder_parity_test $(LIBS_M)
	./$(BIN_DIR)/dw_ladder_parity_test

regen-dw-ladder-parity: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/simulation $(DW_LADDER_PARITY_SRC) -o $(BIN_DIR)/dw_ladder_parity_test $(LIBS_M)
	./$(BIN_DIR)/dw_ladder_parity_test --dump > $(DW_LADDER_PARITY_TXT)
	@echo "wrote $(DW_LADDER_PARITY_TXT) -- now: lua ../NOOBTOPIA/tests/deadweight_ladder_test.lua"

# shankpit_netd -- the internet-of-SHANKPIT shim's HTTP half (tools/shankpit_netd.c). It serves the
# snapshot dw_net.c writes; nothing here links the game. POSIX only, by design: a deployment daemon
# behind WOTAN's nginx, never something a player runs.
netd: $(BIN_DIR)/shankpit_netd

$(BIN_DIR)/shankpit_netd: tools/shankpit_netd.c | $(BIN_DIR)
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror tools/shankpit_netd.c -o $@

# net-demo -- build the daemon and point it at the snapshot `make test-deadweight` writes, so the
# WOTAN page has something real to read on a box where no world is running.
.PHONY: net-demo
net-demo: netd
	./$(BIN_DIR)/shankpit_netd --snapshot var/deadweight/net.json --port 8791

# The fixed-wing transport, headless: takeoff roll, stall, flaps, the v^2 lift law and its inertia.
.PHONY: test-airplane-flight
test-airplane-flight: | $(BIN_DIR)
	$(CC) -O1 -Wall $(INCLUDES) packages/simulation/airplane_flight_test.c packages/world/terrain.c packages/simulation/airplane_rules.c -o $(BIN_DIR)/airplane_flight_test $(LIBS_M)
	./$(BIN_DIR)/airplane_flight_test

test-heli-flight: | $(BIN_DIR)
	$(CC) -O1 -Wall -Ipackages/common -Ipackages/simulation packages/simulation/heli_flight_test.c packages/simulation/heli_rules.c packages/simulation/rocket_car_rules.c packages/simulation/drone_rules.c packages/simulation/airplane_rules.c packages/world/terrain.c -o $(BIN_DIR)/heli_flight_test -lm
	$(BIN_DIR)/heli_flight_test

# ---- the OG train-mode net as a zombie brain (zombie_brain.h) --------------------------------------
.PHONY: regen-zombie-brain
regen-zombie-brain:
	scripts/gen_zombie_brain_rules.sh

# The net itself is compiled-in weights (brain_weights.h), so this needs no checkpoint file at run time.
# What is asserted: determinism, the hand-derived per-class turn caps and lunge thresholds, that five classes
# fed the SAME observation produce genuinely different intents, that NaN is refused rather than propagated,
# and that switching the brain off leaves a zeroed intent (the old path, untouched).
.PHONY: test-zombie-brain
test-zombie-brain: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/simulation packages/simulation/zombie_brain_test.c packages/simulation/zombie_brain.c packages/simulation/zombie_brain_rules.c -o $(BIN_DIR)/zombie_brain_test $(LIBS_M)
	./$(BIN_DIR)/zombie_brain_test

# The lighting/material parity fixtures are asserted from BOTH sides: this repo's env_parity_test.c and
# NOOBTOPIA's tests/{lighting,material}_test.lua read the same two files under PARENA/stdlib/shankpit/. The C
# emitted from those .prn files is authoritative, so a drifting Luau port goes red instead of diverging quietly.
.PHONY: test-env-parity
test-env-parity: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation packages/render/env_parity_test.c \
		packages/simulation/lighting_rules.c packages/simulation/material_rules.c -o $(BIN_DIR)/env_parity_test $(LIBS_M)
	./$(BIN_DIR)/env_parity_test

# The sky rules (day/night, weather intensities, fog visibility, lightning) are PARENA's, emitted to C here and
# to Luau in NOOBTOPIA. This asserts the generated C against the shared fixture; NOOBTOPIA's tests/sky_test.lua
# asserts the same rows, so the two worlds cannot disagree about what a storm looks like.
.PHONY: test-sky-parity
test-sky-parity: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation packages/render/sky_parity_test.c \
		packages/simulation/sky_rules.c -o $(BIN_DIR)/sky_parity_test $(LIBS_M)
	./$(BIN_DIR)/sky_parity_test

# The cosy verbs: kicking a dandelion and hugging a tree. PARENA decides when they are allowed and what they give;
# this builds the host against the generated rules and asserts the shared fixture NOOBTOPIA's tests/cozy_test.lua
# asserts too.
.PHONY: test-cozy
test-cozy: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation packages/simulation/cozy_test.c \
		packages/simulation/cozy_rules.c -o $(BIN_DIR)/cozy_test $(LIBS_M)
	./$(BIN_DIR)/cozy_test

# ---- SHANKPIT on Roblox (roblox/): the 460 layer that sits beside the base place -------------------
# The base place (shankpit_roblox.rbxl) is binary and stays untouched; these targets test the pure Lua modules
# under plain Lua and pack them into one insertable .rbxmx model.
.PHONY: test-roblox roblox-model
test-roblox:
	@lua_bin=$$(command -v lua || command -v luajit); \
	luac_bin=$$(command -v luac || true); \
	if [ -z "$$lua_bin" ]; then echo "SKIP test-roblox: no lua/luajit interpreter on this host"; else \
	  if [ -n "$$luac_bin" ]; then "$$luac_bin" -p roblox/src/*.lua roblox/tests/*.lua || exit 1; \
	    echo "syntax: every roblox/src + roblox/tests file parses"; \
	  else echo "note: no luac on this host -- syntax pass skipped"; fi; \
	  "$$lua_bin" roblox/tests/net460_test.lua && \
	  "$$lua_bin" roblox/tests/queue460_test.lua && \
	  "$$lua_bin" roblox/tests/ticket460_test.lua && \
	  "$$lua_bin" roblox/tests/login460_test.lua && \
	  "$$lua_bin" roblox/tests/stats460_test.lua && \
	  "$$lua_bin" roblox/tests/arena460_test.lua && \
	  "$$lua_bin" roblox/tests/install460_test.lua; fi

# Both artifacts come from ONE PARTS list (roblox/tools/build_rbxmx.py), so the model and the paste script cannot
# disagree about what the layer contains. install460_test runs the paste script against a stubbed Roblox API and
# compares every embedded source to src/, so a stale artifact fails the suite instead of a Studio session.
roblox-model: test-roblox
	python3 roblox/tools/build_rbxmx.py
	python3 roblox/tools/build_installer.py

# One Command Bar paste: the same layer, no Insert From File and no drag-and-drop.
.PHONY: roblox-install
roblox-install:
	python3 roblox/tools/build_installer.py

# The 460 layer's thresholds and ticket arithmetic are PARENA's (stdlib/shankpit/{queue,ticket}_rules.prn),
# emitted to Luau. Never hand-edit
# roblox/src/queue460_rules.lua; change the .prn and regenerate.
.PHONY: regen-roblox-rules
regen-roblox-rules:
	roblox/scripts/gen_460_rules.sh
