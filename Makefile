# 🛹 SHANKPIT Master Makefile (CI-safe, deterministic outputs)

# ---- Tooling ----
CC       := gcc
BIN_DIR  := bin

# ---- Flags ----
CFLAGS   := -O2 -Wall -D_REENTRANT
INCLUDES := -Ipackages/common -Ipackages/simulation -Ipackages/render -Ipackages/world

LIBS_GL  := -lSDL2 -lGL -lGLU -lm
LIBS_M   := -lm

# ---- Sources ----
LOBBY_SRC    := apps/lobby/src/main.c packages/render/proc_tex.c packages/render/retro_material.c packages/render/retro_sky.c packages/render/retro_lighting.c packages/world/terrain.c
SERVER_SRC   := apps/server/src/main.c packages/world/terrain.c
SERVERCTL_SRC:= apps/server/serverctl.c
SCENE_EDITOR_SRC := apps/scene_editor/main.c apps/scene_editor/editor_app.c apps/scene_editor/editor_camera.c apps/scene_editor/editor_selection.c apps/scene_editor/editor_move_tool.c apps/scene_editor/editor_ui.c apps/scene_editor/editor_scene_asset.c apps/scene_editor/editor_scene_json.c

# ---- Outputs ----
LOBBY_BIN    := $(BIN_DIR)/shank_lobby
SERVER_BIN   := $(BIN_DIR)/shank_server
SERVERCTL_BIN:= $(BIN_DIR)/serverctl
SCENE_EDITOR_BIN := $(BIN_DIR)/shank_scene_editor

# ---- Targets ----
.PHONY: all lobby server serverctl scene_editor clean setup print

all: $(LOBBY_BIN) $(SERVER_BIN) $(SCENE_EDITOR_BIN)

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

# ---- SCENE EDITOR (STANDALONE TOOL) ----
scene_editor: $(SCENE_EDITOR_BIN)

$(SCENE_EDITOR_BIN): $(SCENE_EDITOR_SRC) | $(BIN_DIR)
	@echo "🧰 Building Scene Editor..."
	$(CC) $(CFLAGS) $(INCLUDES) $(SCENE_EDITOR_SRC) -o $@ $(LIBS_GL)

clean:
	@echo "🧹 Cleaning..."
	rm -rf $(BIN_DIR)

print:
	@echo "LOBBY_BIN=$(LOBBY_BIN)"
	@echo "SERVER_BIN=$(SERVER_BIN)"
	@echo "SCENE_EDITOR_BIN=$(SCENE_EDITOR_BIN)"
