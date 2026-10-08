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
LOBBY_SRC    := apps/lobby/src/main.c apps/lobby/src/editor_widget_bridge.c packages/simulation/story_ai.c packages/simulation/ai_nav.c packages/simulation/humanness.c packages/simulation/cutscene.c packages/simulation/typing_lesson.c packages/simulation/tyler_coldopen.c packages/simulation/tyler_e03_coldopen.c packages/simulation/tyler_fb01_coldopen.c packages/simulation/tyler_voice_mod.c packages/simulation/day_night_clock.c packages/simulation/world_rules.c packages/simulation/witness_ai.c packages/simulation/witness_sim.c packages/simulation/witness_rules.c packages/simulation/npc_archetype.c packages/simulation/zombie_values.c packages/simulation/avian_values.c packages/simulation/ai_brain_rules.c packages/simulation/world_alerts_mod.c packages/simulation/world_alert_bridge.c packages/simulation/food_pickup.c packages/simulation/lab_station_host.c packages/simulation/lab_station_rules.c packages/simulation/gun_items.c packages/simulation/chests.c packages/simulation/chest_rules.c packages/simulation/shield_packs.c packages/simulation/giant_bug_values.c packages/simulation/giant_bug_brain.c packages/render/proc_tex.c packages/render/retro_material.c packages/render/retro_sky.c packages/render/sky_weather.c packages/render/retro_lighting.c packages/render/gl_shader.c packages/render/bloom.c packages/world/terrain.c packages/world/parena_runtime.c packages/world/png_decode_gen.c packages/ui/menu_gen.c packages/audio/audio.c packages/audio/audio_chain.c packages/audio/audio_dsp_gen.c packages/goldenband/gband.c packages/goldenband/gskel.c packages/goldenband/gmesh.c packages/goldenband/gband_mesh_rig.c packages/goldenband/gseq.c packages/goldenband/gpose.c packages/goldenband/gsync.c packages/goldenband/gband_skel_npc.c packages/reflux/reflux_runtime.c packages/reflux/reflux_mod.c packages/simulation/brick_mod.c packages/simulation/ambient_rules.c packages/simulation/buggy_rules.c packages/simulation/heli_rules.c packages/render/camera_rules.c packages/simulation/ragdoll_pool.c packages/simulation/rigid_ragdoll.c packages/simulation/ragdoll_rules.c packages/goldenband/grb.c $(EDU_LOBBY_SRC)
SERVER_SRC   := apps/server/src/main.c packages/simulation/shield_packs.c packages/simulation/story_ai.c packages/simulation/ai_nav.c packages/simulation/humanness.c packages/simulation/tyler_coldopen.c packages/simulation/tyler_e03_coldopen.c packages/simulation/tyler_fb01_coldopen.c packages/simulation/tyler_voice_mod.c packages/simulation/day_night_clock.c packages/simulation/world_rules.c packages/simulation/witness_ai.c packages/simulation/witness_sim.c packages/simulation/witness_rules.c packages/simulation/npc_archetype.c packages/simulation/zombie_values.c packages/simulation/avian_values.c packages/simulation/ai_brain_rules.c packages/simulation/giant_bug_values.c packages/simulation/giant_bug_brain.c packages/world/terrain.c packages/reflux/reflux_runtime.c packages/reflux/reflux_mod.c packages/simulation/brick_mod.c packages/simulation/buggy_rules.c packages/simulation/heli_rules.c
SERVERCTL_SRC:= apps/server/serverctl.c

# ---- Source-list export for CI ----
# CI (.github/workflows/{tests,release}.yml) compiles the Windows lobby and the Linux server with
# raw gcc/mingw lines instead of `make` (different libs/flags). Those lines used to carry a
# hand-copied copy of LOBBY_SRC/SERVER_SRC and drifted NINE times -- including a 3-day red release
# pipeline (2026-09-28..10-01) -- so they now expand these two targets instead:
#   gcc $(make -s print-server-src) ...        x86_64-w64-mingw32-gcc $(make -s print-lobby-src) ...
# A new packages/*.c added to LOBBY_SRC/SERVER_SRC above is now picked up by CI automatically.
.PHONY: print-lobby-src print-server-src
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
.PHONY: test-witness-ai-zombies test-bullet-hole test-queue-level test-baked-levels bake-levels all lobby server serverctl clean setup print go-server ea ea-windows emily-bot rigid-ragdoll test-physics test-audio-chain ux-screenshot-test

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

# ---- CLIENT / LOBBY ----
lobby: $(LOBBY_BIN)

$(LOBBY_BIN): $(LOBBY_SRC) $(EDITOR_GAME_OBJS) | $(BIN_DIR)
	@echo "🔨 Building Lobby Client..."
	$(CC) $(CFLAGS) $(INCLUDES) $(LOBBY_SRC) $(EDITOR_GAME_OBJS) -o $@ $(LIBS_GL) -lSDL2_ttf

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
	x86_64-w64-mingw32-gcc $(CFLAGS) $(INCLUDES) -I$(SDL2_MINGW_PREFIX)/include $(LOBBY_SRC) $(EDITOR_GAME_OBJS_WIN) \
		-o $(BIN_DIR)/shank_lobby.exe \
		-L$(SDL2_MINGW_PREFIX)/lib -lSDL2 -lSDL2_ttf -lopengl32 -lglu32 -lm -static-libgcc \
		-lws2_32 -ldinput8 -ldxguid -ldxerr8 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lsetupapi -lversion -luuid
	@echo "📦 Packaging EA build (Windows)..."
	@mkdir -p dist/ea-windows
	@cp $(BIN_DIR)/shank_go_server.exe dist/ea-windows/
	@cp $(BIN_DIR)/shank_lobby.exe dist/ea-windows/
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
	$(CC) -std=c99 -O2 -Wall -Wextra -Ipackages/world -o /tmp/bullet_hole_test packages/world/bullet_hole_test.c \
		packages/world/png_decode_gen.c packages/world/parena_runtime.c -lm
	/tmp/bullet_hole_test

# test-queue-level -- PACKET_QUEUE_LEVEL wire contract (card #481): size, id round-trip incl. -1, unique type.
test-queue-level:
	$(CC) -std=c99 -O2 -Wall -Wextra -pedantic -Werror -Ipackages/common -o /tmp/queue_level_test packages/common/queue_level_test.c
	/tmp/queue_level_test

# test-shield-packs -- card #541: 25% drop rule, once-per-death latch, pickup reach, wire contract of PACKET_SHIELD_PACKS.
test-shield-packs:
	$(CC) -std=c99 -O2 -Wall -Wextra -pedantic -Werror -Ipackages/common -o /tmp/shield_packs_test packages/simulation/shield_packs_test.c packages/simulation/shield_packs.c -lm
	/tmp/shield_packs_test

# bake-levels -- card #479: regenerate packages/world/baked_levels_gen.h from a live IDUNA (review + commit the diff).
bake-levels:
	python3 scripts/bake_levels.py

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

WAI_TEST_SRC := packages/simulation/witness_ai.c packages/simulation/witness_sim.c packages/simulation/witness_rules.c packages/simulation/npc_archetype.c packages/simulation/zombie_values.c packages/simulation/avian_values.c packages/simulation/ai_brain_rules.c packages/simulation/giant_bug_values.c packages/simulation/giant_bug_brain.c packages/simulation/day_night_clock.c packages/simulation/world_rules.c packages/simulation/humanness.c packages/reflux/reflux_runtime.c packages/reflux/reflux_mod.c
test-witness-ai-zombies: | $(BIN_DIR)
	$(CC) -g -O1 -fsanitize=address,undefined -Wall $(INCLUDES) packages/simulation/witness_ai_zombies_test.c $(WAI_TEST_SRC) -o $(BIN_DIR)/witness_ai_zombies_test $(LIBS_M)
	./$(BIN_DIR)/witness_ai_zombies_test

test-heli-flight: | $(BIN_DIR)
	$(CC) -O1 -Wall -Ipackages/common -Ipackages/simulation packages/simulation/heli_flight_test.c packages/simulation/heli_rules.c packages/world/terrain.c -o $(BIN_DIR)/heli_flight_test -lm
	$(BIN_DIR)/heli_flight_test
