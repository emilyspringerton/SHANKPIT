# Destructible brick (packages/world/brick_fracture.h + packages/simulation/brick_mod.c, the latter
# generated from PARENA by scripts/gen_brick_mod.sh). Headless: strict flags, ASan+UBSan.
.PHONY: test-brick-fracture
test-brick-fracture:
	gcc -std=c99 -Wall -Wextra -pedantic -Werror -g -fsanitize=address,undefined \
		-Ipackages/world -Ipackages/simulation \
		packages/world/brick_fracture_test.c packages/simulation/brick_mod.c \
		-o /tmp/shankpit_brick_fracture_test -lm
	/tmp/shankpit_brick_fracture_test

# Through the real engine paths (physics.h update_weapons + custom-level slots + trace_map + the
# brick_world.h glue): headless, ASan+UBSan.
.PHONY: test-brick-world test-brick brick-e2e
test-brick-world: $(STRUCTURE_RULES_C)
	gcc -std=gnu99 -Wall -g -fsanitize=address,undefined $(INCLUDES) \
		packages/simulation/brick_world_test.c packages/simulation/brick_mod.c packages/world/terrain.c \
		packages/simulation/brick_bodies.c packages/goldenband/grb.c \
		packages/simulation/structure_rules.c packages/world/parena_runtime.c \
		-o /tmp/shankpit_brick_world_test -lm
	/tmp/shankpit_brick_world_test

# Physical broken brick on GOLDEN BAND grb: mass, falling, piling on the floor, sliding down a ramp.
.PHONY: test-brick-bodies
test-brick-bodies:
	gcc -std=gnu99 -Wall -Wextra -Werror -g -fsanitize=address,undefined -Ipackages/goldenband -Ipackages/simulation \
		packages/simulation/brick_bodies_test.c packages/simulation/brick_bodies.c packages/goldenband/grb.c \
		-o /tmp/shankpit_brick_bodies_test -lm
	/tmp/shankpit_brick_bodies_test

# A NOCK material's density (kg/m^3) parses into the native level loader.
.PHONY: test-brick-density
test-brick-density:
	gcc -std=gnu99 -Wall -Wextra -Werror -g -fsanitize=address,undefined $(INCLUDES) packages/world/level_boxes_density_test.c \
		-o /tmp/shankpit_level_boxes_density_test -lm
	/tmp/shankpit_level_boxes_density_test

test-brick: test-brick-fracture test-brick-world test-brick-bodies test-brick-density

# Real server + real packet clients: a shooter destroys a brick cell, a late joiner learns of it.
# Needs python3 + numpy (scripts/rl_env_packet.py's imports).
brick-e2e: server
	python3 scripts/brick_e2e.py

# Restored SCENE_CITY procedural geometry (packages/common/physics.h init_city_geo).
.PHONY: test-city
test-city:
	gcc -std=gnu99 -Wall -g -fsanitize=address,undefined $(INCLUDES) \
		packages/common/city_geo_test.c packages/world/terrain.c \
		-o /tmp/shankpit_city_geo_test -lm
	/tmp/shankpit_city_geo_test

.PHONY: test-third-person
test-third-person:
	gcc -std=gnu99 -Wall -g -fsanitize=address,undefined $(INCLUDES) \
		packages/common/third_person_test.c packages/world/terrain.c \
		-o /tmp/shankpit_third_person_test -lm
	/tmp/shankpit_third_person_test

.PHONY: test-wall-ride
test-wall-ride:
	gcc -std=gnu99 -Wall -g -fsanitize=address,undefined $(INCLUDES) \
		packages/common/wall_ride_test.c packages/world/terrain.c \
		-o /tmp/shankpit_wall_ride_test -lm
	/tmp/shankpit_wall_ride_test

.PHONY: test-buggy-wall
test-buggy-wall:
	gcc -std=gnu99 -Wall -g -fsanitize=address,undefined $(INCLUDES) \
		packages/common/buggy_wall_test.c packages/world/terrain.c packages/simulation/buggy_rules.c \
		-o /tmp/shankpit_buggy_wall_test -lm
	/tmp/shankpit_buggy_wall_test
	gcc -std=gnu99 -Wall -Wextra -g -fsanitize=address,undefined $(INCLUDES) \
		packages/world/level_boxes_buggy_test.c -o /tmp/shankpit_level_boxes_buggy_test -lm
	/tmp/shankpit_level_boxes_buggy_test

.PHONY: test-litbox-render
test-litbox-render:
	gcc -std=gnu99 -Wall -g $(INCLUDES) packages/render/litbox_render_test.c packages/render/gl_shader.c \
		-o /tmp/shankpit_litbox_render_test -lSDL2 -lGL -lm
	xvfb-run -a -s "-screen 0 640x480x24" env LIBGL_ALWAYS_SOFTWARE=1 /tmp/shankpit_litbox_render_test

.PHONY: test-ambient-boost
test-ambient-boost:
	gcc -std=gnu99 -Wall -g -fsanitize=address,undefined $(INCLUDES) packages/render/ambient_boost_test.c \
		packages/render/retro_lighting.c packages/render/env_matrix.c packages/render/retro_sky.c packages/render/proc_tex.c packages/render/retro_material.c packages/render/sky_weather.c packages/simulation/ambient_rules.c \
		packages/world/parena_runtime.c -o /tmp/shankpit_ambient_boost_test -lm -lGL
	/tmp/shankpit_ambient_boost_test

# test-env-matrix -- the unified environment matrix: the PARENA lighting rules (clamped, pure,
# total), the host layer's unit conversion, and the regression that started this (a stormy night at
# ambient slider 0 must not be near-black through the REAL retro_lighting surface path).
# Also guards the emitter trade-off (fog shortens reach, widens spread) and determinism.
.PHONY: test-env-matrix
test-env-matrix: $(LIGHTING_RULES_C)
	gcc -std=gnu99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
		-Ipackages/render -Ipackages/simulation -Ipackages/world \
		packages/render/env_matrix_test.c packages/render/env_matrix.c \
		packages/simulation/lighting_rules.c packages/world/parena_runtime.c \
		-o /tmp/shankpit_env_matrix_test -lm
	/tmp/shankpit_env_matrix_test

# The generated PARENA rules must exist; regenerate them if the .prn is newer.
# test-lighting-chain -- the whole chain end to end (PARENA rules -> env_matrix -> retro_lighting ->
# the surface term), swept over every preset x weather x slider x face direction. Asserts the
# invariants that must hold for ALL of them: the matrix only ever adds light, a shaded face never
# ends below the floor, nothing escapes [0,1], the day/night swing survives, and the result is
# deterministic. retro_sky.c is GL-bound, so the sun position/colour source is stubbed.
.PHONY: test-lighting-chain
test-lighting-chain: $(LIGHTING_RULES_C)
	gcc -std=gnu99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
		-Ipackages/render -Ipackages/simulation -Ipackages/world \
		packages/render/lighting_chain_test.c packages/render/retro_lighting.c \
		packages/render/env_matrix.c packages/simulation/lighting_rules.c \
		packages/world/parena_runtime.c -o /tmp/shankpit_lighting_chain_test -lm
	/tmp/shankpit_lighting_chain_test

# test-structure -- the structural layer over destructible brick: intersection joints at load
# (anchor vs peg, and glass rated too weak to join), the support flood, and the load cascade where an
# overloaded pillar fails and the slab above it stops being supported. Numbers are PARENA
# (stdlib/shankpit/structure_rules.prn); the graph arithmetic is pixel-free, so this is headless.
.PHONY: test-structure
test-structure: $(STRUCTURE_RULES_C)
	gcc -std=gnu99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
		-Ipackages/simulation -Ipackages/world -Ipackages/common \
		packages/simulation/structure_test.c packages/simulation/structure_rules.c \
		packages/world/parena_runtime.c -o /tmp/shankpit_structure_test -lm
	/tmp/shankpit_structure_test

# ---- ARCHIPELAGO water (docs/ARCHIPELAGO_NORTHSTAR.md) ------------------------------------------------
# Sea state, wave field, planing lift and drag are PARENA's (stdlib/shankpit/ocean_rules.prn); ocean.h samples and
# integrates them. First water code in the engine, so the test is also the spec.
# EVERY generated .c below is COMMITTED, so a checkout WITHOUT the sibling ../PARENA (or
# ../NOOBTOPIA) must still build. Declaring these prerequisites unconditionally made make fail on
# such a machine with `No rule to make target '../PARENA/stdlib/shankpit/lighting_rules.prn'`, which
# is a build break with no code behind it -- the generator rule can only exist when its source is
# actually here, and regeneration is an explicit `make regen-*` job.
OCEAN_RULES_PRN ?= ../PARENA/stdlib/shankpit/ocean_rules.prn
OCEAN_RULES_C   ?= packages/simulation/ocean.c
ifneq ($(wildcard $(OCEAN_RULES_PRN)),)
$(OCEAN_RULES_C): $(OCEAN_RULES_PRN)
	./scripts/gen_ocean_rules.sh
endif

.PHONY: test-ocean
test-ocean: $(OCEAN_RULES_C)
	gcc -std=gnu99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
		-Ipackages/simulation -Ipackages/world -Ipackages/common \
		packages/simulation/ocean_test.c packages/simulation/ocean.c \
		-o /tmp/shankpit_ocean_test -lm
	/tmp/shankpit_ocean_test

# ---- the NOOBTOPIA three-way merge (MONOREPO/docs/THREE_WAY_MERGE.md) --------------------------------
# Decisions are PARENA's, once (stdlib/noobtopia/*.prn), emitted to C here and to Luau in NOOBTOPIA. The WORDS are
# NOOBTOPIA's data file, generated into ambient_lines.inc. Neither side keeps a copy.
NOOBTOPIA_RULES_PRN ?= ../PARENA/stdlib/noobtopia/ambient_life.prn ../PARENA/stdlib/noobtopia/walk_rules.prn ../PARENA/stdlib/noobtopia/arena_rules.prn
NOOBTOPIA_RULES_C   ?= packages/simulation/ambient_life.c packages/simulation/walk_rules.c packages/simulation/arena_rules.c
AMBIENT_LINES_INC   ?= packages/simulation/ambient_lines.inc

ifneq ($(wildcard $(firstword $(NOOBTOPIA_RULES_PRN))),)
$(NOOBTOPIA_RULES_C): $(NOOBTOPIA_RULES_PRN)
	./scripts/gen_noobtopia_rules.sh
endif

ifneq ($(wildcard ../NOOBTOPIA/data/noob_lines.lua),)
$(AMBIENT_LINES_INC): ../NOOBTOPIA/data/noob_lines.lua
	./scripts/gen_ambient_lines.sh
endif

.PHONY: test-ambient-life test-walk-rules test-noobtopia-arena test-noobtopia
test-ambient-life: $(NOOBTOPIA_RULES_C) $(AMBIENT_LINES_INC)
	gcc -std=gnu99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
		-Ipackages/simulation -Ipackages/world -Ipackages/common \
		packages/simulation/ambient_life_test.c packages/simulation/ambient_life.c \
		-o /tmp/shankpit_ambient_life_test -lm
	/tmp/shankpit_ambient_life_test

test-noobtopia-arena: $(NOOBTOPIA_RULES_C)
	gcc -std=gnu99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
		-Ipackages/simulation -Ipackages/world -Ipackages/common \
		packages/simulation/arena_rules_test.c packages/simulation/arena_rules.c \
		-o /tmp/shankpit_arena_rules_test -lm
	/tmp/shankpit_arena_rules_test

# Where it drifts, and when: the shared walk rule (PARENA walk_rules.prn), with the same parity vectors
# NOOBTOPIA/tests/walk_test.lua asserts -- a startled noob frozen, a curious one roaming, the leash holding.
test-walk-rules: $(NOOBTOPIA_RULES_C)
	gcc -std=gnu99 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
		-Ipackages/simulation -Ipackages/world -Ipackages/common \
		packages/simulation/walk_rules_test.c packages/simulation/walk_rules.c \
		-o /tmp/shankpit_walk_rules_test -lm
	/tmp/shankpit_walk_rules_test

test-noobtopia: test-ambient-life test-walk-rules test-noobtopia-arena

STRUCTURE_RULES_PRN ?= ../PARENA/stdlib/shankpit/structure_rules.prn
STRUCTURE_RULES_C   ?= packages/simulation/structure_rules.c
ifneq ($(wildcard $(STRUCTURE_RULES_PRN)),)
$(STRUCTURE_RULES_C): $(STRUCTURE_RULES_PRN)
	./scripts/gen_structure_rules.sh
endif

LIGHTING_RULES_PRN ?= ../PARENA/stdlib/shankpit/lighting_rules.prn
LIGHTING_RULES_C   ?= packages/simulation/lighting_rules.c
ifneq ($(wildcard $(LIGHTING_RULES_PRN)),)
$(LIGHTING_RULES_C): $(LIGHTING_RULES_PRN)
	./scripts/gen_lighting_rules.sh
endif

.PHONY: test-hammer
test-hammer:
	gcc -std=gnu99 -Wall -g -fsanitize=address,undefined $(INCLUDES) packages/common/hammer_test.c packages/world/terrain.c \
		-o /tmp/shankpit_hammer_test -lm
	/tmp/shankpit_hammer_test

# HAMMER MINI-TOWER -- weapon 8's E power (packages/simulation/hammer_tower.h): the 3.0 s cast
# with its 2.6 s still requirement and FFXI release window, and the gatling tower it builds --
# zombies always, The Men and citizens only in combat, weak, expiring, never aimed at the player.
.PHONY: test-hammer-tower
test-hammer-tower: | $(BIN_DIR)
	$(CC) -g -O1 -fsanitize=address,undefined -Wall $(INCLUDES) packages/simulation/hammer_tower_test.c packages/simulation/hammer_tower.c $(WAI_TEST_SRC) packages/world/terrain.c -o $(BIN_DIR)/hammer_tower_test $(LIBS_M)
	./$(BIN_DIR)/hammer_tower_test

# test-ar-grenade -- the assault rifle's underbarrel grenade (E and the right-mouse/LT secondary
# bit): one real projectile with the grenade's own damage/splash/gravity/owning-weapon, a speed that
# is faster than a missile but not instant, both inputs against one shared cooldown, and the
# guarantee that the secondary bit never spends the sniper's storm charges under zoom.
test-ar-grenade:
	gcc -std=gnu99 -Wall -g $(INCLUDES) packages/simulation/ar_grenade_test.c packages/world/terrain.c \
		-o /tmp/shankpit_ar_grenade_test -lm
	/tmp/shankpit_ar_grenade_test

.PHONY: test-held-model
test-held-model:
	gcc -std=gnu99 -Wall -Wextra -g -fsanitize=address,undefined $(INCLUDES) packages/render/held_model_test.c \
		-o /tmp/shankpit_held_model_test -lm
	/tmp/shankpit_held_model_test

.PHONY: test-glass-render
test-glass-render:
	gcc -std=gnu99 -Wall -g $(INCLUDES) packages/render/glass_render_test.c packages/render/gl_shader.c \
		-o /tmp/shankpit_glass_render_test -lSDL2 -lGL -lm
	xvfb-run -a -s "-screen 0 640x480x24" env LIBGL_ALWAYS_SOFTWARE=1 /tmp/shankpit_glass_render_test

# Pheromone thrower: the canister's physics (mass, arc, bounce, rest) and the infighting it sets off.
.PHONY: test-pheromone-grenade
test-pheromone-grenade: | $(BIN_DIR)
	$(CC) -g -O1 -fsanitize=address,undefined -Wall $(INCLUDES) packages/simulation/pheromone_grenade_test.c $(WAI_TEST_SRC) -o $(BIN_DIR)/pheromone_grenade_test $(LIBS_M)
	./$(BIN_DIR)/pheromone_grenade_test
