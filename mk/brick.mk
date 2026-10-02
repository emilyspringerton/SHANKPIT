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
test-brick-world:
	gcc -std=gnu99 -Wall -g -fsanitize=address,undefined $(INCLUDES) \
		packages/simulation/brick_world_test.c packages/simulation/brick_mod.c packages/world/terrain.c \
		-o /tmp/shankpit_brick_world_test -lm
	/tmp/shankpit_brick_world_test

test-brick: test-brick-fracture test-brick-world

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
		packages/render/retro_lighting.c packages/render/retro_sky.c packages/render/proc_tex.c packages/render/retro_material.c packages/render/sky_weather.c packages/simulation/ambient_rules.c \
		packages/world/parena_runtime.c -o /tmp/shankpit_ambient_boost_test -lm -lGL
	/tmp/shankpit_ambient_boost_test

.PHONY: test-hammer
test-hammer:
	gcc -std=gnu99 -Wall -g -fsanitize=address,undefined $(INCLUDES) packages/common/hammer_test.c packages/world/terrain.c \
		-o /tmp/shankpit_hammer_test -lm
	/tmp/shankpit_hammer_test

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
