# local NOCK (tools/local_nock.py): browser level editor + live edit-session server that the native
# client joins through SHANKPIT_EDIT_SESSIONS_URL. Levels save into levels/*.json and are committed.
LOCAL_NOCK_PORT ?= 8787

local-nock:
	python3 tools/local_nock.py --port $(LOCAL_NOCK_PORT)

# local-nock-parena -- the same server, built from tools/local_nock.prn. Requires a sibling ../PARENA
# checkout with a built .parena-bootstrap (the bootstrap, not `parena`, since the full binary needs
# SDL2 headers this tool never uses). The generated C is not checked in.
PARENA_DIR ?= ../PARENA
LOCAL_NOCK_GEN ?= var/local_nock_gen.c
LOCAL_NOCK_BIN ?= bin/local_nock_parena

# Every PARENA input the binary is built from is a prerequisite, not just the .prn: a stdlib or
# runtime change must rebuild it (a stale binary silently tested an older http_server once already).
PARENA_SRC := $(PARENA_DIR)/stdlib/string.prn $(PARENA_DIR)/stdlib/net/tcp.prn \
              $(PARENA_DIR)/stdlib/net/http_server.prn $(PARENA_DIR)/stdlib/process.prn \
              $(PARENA_DIR)/runtime/parena_runtime.h $(PARENA_DIR)/runtime/parena_runtime.c

$(LOCAL_NOCK_BIN): tools/local_nock.prn tools/local_nock_host.c tools/local_nock_host.h $(PARENA_SRC)
	@test -x $(PARENA_DIR)/.parena-bootstrap || \
	  { echo "need $(PARENA_DIR)/.parena-bootstrap (cd $(PARENA_DIR) && make .parena-bootstrap)"; exit 1; }
	@mkdir -p var bin
	@$(MAKE) -C $(PARENA_DIR) .parena-bootstrap >/dev/null
	$(PARENA_DIR)/.parena-bootstrap build \
	  $(PARENA_DIR)/stdlib/string.prn $(PARENA_DIR)/stdlib/net/tcp.prn \
	  $(PARENA_DIR)/stdlib/net/http_server.prn $(PARENA_DIR)/stdlib/process.prn \
	  tools/local_nock.prn -o $(LOCAL_NOCK_GEN)
	gcc -std=c99 -pedantic -Wall -Wextra -Werror -DPARENA_NO_GRAPHICS \
	  -I $(PARENA_DIR)/runtime -I tools -include tools/local_nock_host.h \
	  $(LOCAL_NOCK_GEN) tools/local_nock_host.c $(PARENA_DIR)/runtime/parena_runtime.c \
	  -o $(LOCAL_NOCK_BIN)

local-nock-parena: $(LOCAL_NOCK_BIN)
	LOCAL_NOCK_PORT=$(LOCAL_NOCK_PORT) ./$(LOCAL_NOCK_BIN)

# test-local-nock-parena -- the SAME checks against the PARENA binary, including the hardening ones
# (stale session id, wrong method, traversal-shaped level names, oversized body). One check is
# honestly different: the PARENA runner is single-threaded, so it answers `wait` immediately and a
# client polls again instead of the server blocking a thread on it.
test-local-nock-parena: $(LOCAL_NOCK_BIN)
	python3 tools/test_local_nock.py --binary ./$(LOCAL_NOCK_BIN)

# test-local-nock -- end-to-end over the real HTTP surface: level save + git commit, session
# create/post/poll, working-draft push/fetch, and the bad-request paths.
test-local-nock:
	python3 tools/test_local_nock.py
