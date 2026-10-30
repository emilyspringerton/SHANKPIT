# leveltool -- headless level JSON utility for the IDUNA-free authoring flow (2026-10-30).
# `info` / `check` / `save` / `new` over packages/world/level_boxes.h's own loader+writer, so a map
# authored with EDIT MAP's F6 can be inspected, created and validated without launching the lobby.
# `make check-levels` round-trips every checked-in level export through it (real regression gate for
# "a save/load cycle drops nothing").
LEVELTOOL_BIN := $(BIN_DIR)/leveltool
LEVELTOOL_JSON := var/lab/lab.json examples/story-doors/demo_level/level.json \
                  var/zombie/nextown_zombies.json $(wildcard assets/tyler_levels/*.json)

leveltool: | $(BIN_DIR)
	$(CC) -std=gnu99 -O2 -Wall -Wextra -Werror -Ipackages/world -o $(LEVELTOOL_BIN) tools/leveltool.c -lm

check-levels: leveltool
	@for f in $(LEVELTOOL_JSON); do $(LEVELTOOL_BIN) check $$f || exit 1; done

test-leveltool: leveltool
	$(LEVELTOOL_BIN) new leveltool-smoke /tmp/leveltool_smoke.json
	$(LEVELTOOL_BIN) check /tmp/leveltool_smoke.json
	$(LEVELTOOL_BIN) info /tmp/leveltool_smoke.json
	$(LEVELTOOL_BIN) map /tmp/leveltool_smoke.json >/dev/null
	$(LEVELTOOL_BIN) map var/lab/lab.json | grep -q '#'
	$(LEVELTOOL_BIN) map var/lab/lab.json --min-height 10 | grep -q 'tall hidden'
	$(LEVELTOOL_BIN) map var/lab/lab.json --cell 20 --center 0,0 | grep -q 'zoom window'
	@test $$($(LEVELTOOL_BIN) map examples/story-doors/demo_level/level.json --cell 1 --center 0,20 | grep -c '#') -gt 5
