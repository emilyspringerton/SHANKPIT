# 🛹 SkateChain / Shank Pit Master Makefile

CC = gcc
# Note: We rely on the Unity Build pattern (main.c includes other .c files)
# so we only need to compile the entry points.

# Flags
CFLAGS = -O2 -Wall -D_REENTRANT
LIBS_LINUX = -lSDL2 -lGL -lGLU -lm
LIBS_MAC = -lSDL2 -framework OpenGL -framework GLUT -lm

# Detect OS
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	LIBS = $(LIBS_MAC)
else
	LIBS = $(LIBS_LINUX)
endif

all: setup client server

setup:
	@mkdir -p bin

client:
	@echo "🔨 Building Client..."
	$(CC) $(CFLAGS) apps/shank-fps/src/main.c -o bin/shank_client $(LIBS)

server:
	@echo "🔨 Building Server..."
	$(CC) $(CFLAGS) services/game-server/src/server.c -o bin/shank_server -lm

server-local:
	@echo "🔨 Building Local Server..."
	$(CC) $(CFLAGS) apps/server/src/main.c -o bin/shank_local_server -lm

clean:
	rm -rf bin/shank_client bin/shank_server bin/shank_local_server bin/test_netcode bin/test_player_model bin/test_server_mode

tests: setup
	@echo "🧪 Building Tests..."
	$(CC) $(CFLAGS) apps/tests/test_netcode.c -o bin/test_netcode
	$(CC) $(CFLAGS) apps/tests/test_player_model.c -o bin/test_player_model -lm
	$(CC) $(CFLAGS) apps/tests/test_server_mode.c -o bin/test_server_mode
