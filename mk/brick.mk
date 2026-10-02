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
