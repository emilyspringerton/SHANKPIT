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
LOBBY_SRC    := apps/lobby/src/main.c packages/simulation/story_ai.c packages/simulation/ai_nav.c packages/simulation/humanness.c packages/simulation/cutscene.c packages/simulation/typing_lesson.c packages/simulation/day_night_clock.c packages/simulation/world_rules.c packages/simulation/witness_ai.c packages/simulation/witness_sim.c packages/simulation/witness_rules.c packages/simulation/npc_archetype.c packages/simulation/zombie_values.c packages/simulation/ai_brain_rules.c packages/render/proc_tex.c packages/render/retro_material.c packages/render/retro_sky.c packages/render/sky_weather.c packages/render/retro_lighting.c packages/render/gl_shader.c packages/render/bloom.c packages/world/terrain.c packages/world/parena_runtime.c packages/world/png_decode_gen.c packages/audio/audio.c packages/goldenband/gband.c packages/goldenband/gskel.c packages/goldenband/gmesh.c packages/goldenband/gband_mesh_rig.c packages/goldenband/gseq.c packages/goldenband/gpose.c packages/goldenband/gsync.c packages/goldenband/gband_skel_npc.c packages/reflux/reflux_runtime.c packages/reflux/reflux_mod.c
SERVER_SRC   := apps/server/src/main.c packages/simulation/story_ai.c packages/simulation/ai_nav.c packages/simulation/humanness.c packages/simulation/witness_ai.c packages/simulation/witness_sim.c packages/simulation/witness_rules.c packages/simulation/npc_archetype.c packages/simulation/zombie_values.c packages/simulation/ai_brain_rules.c packages/world/terrain.c packages/reflux/reflux_runtime.c packages/reflux/reflux_mod.c
SERVERCTL_SRC:= apps/server/serverctl.c

# ---- Outputs ----
LOBBY_BIN    := $(BIN_DIR)/shank_lobby
SERVER_BIN   := $(BIN_DIR)/shank_server
SERVERCTL_BIN:= $(BIN_DIR)/serverctl
GO_SERVER_BIN := $(BIN_DIR)/shank_go_server
EMILY_BOT_BIN := $(BIN_DIR)/emily-bot
EA_DIR       := dist/ea

# ---- Targets ----
.PHONY: all lobby server serverctl clean setup print go-server ea ea-windows emily-bot

all: $(LOBBY_BIN) $(SERVER_BIN)

# Ensure bin/ exists even when building a single target
$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

setup: $(BIN_DIR)

# ---- CLIENT / LOBBY ----
lobby: $(LOBBY_BIN)

$(LOBBY_BIN): $(LOBBY_SRC) | $(BIN_DIR)
	@echo "🔨 Building Lobby Client..."
	$(CC) $(CFLAGS) $(INCLUDES) $(LOBBY_SRC) -o $@ $(LIBS_GL)

# ---- GAME SERVER ----
server: $(SERVER_BIN)

$(SERVER_BIN): $(SERVER_SRC) | $(BIN_DIR)
	@echo "🔨 Building Game Server..."
	$(CC) $(CFLAGS) $(INCLUDES) $(SERVER_SRC) -o $@ $(LIBS_M)

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
ea-windows: $(BIN_DIR)
	@echo "🪟 Cross-compiling Go server for Windows..."
	GOWORK=off GOOS=windows GOARCH=amd64 go build -o $(BIN_DIR)/shank_go_server.exe ./apps2/server-go/
	@echo "🔨 Cross-compiling C client for Windows (requires x86_64-w64-mingw32-gcc)..."
	x86_64-w64-mingw32-gcc $(CFLAGS) $(INCLUDES) -I$(SDL2_MINGW_PREFIX)/include $(LOBBY_SRC) \
		-o $(BIN_DIR)/shank_lobby.exe \
		-L$(SDL2_MINGW_PREFIX)/lib -lSDL2 -lopengl32 -lglu32 -lm -static-libgcc \
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
