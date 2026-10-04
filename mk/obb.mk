# Oriented boxes + ramps (packages/common/obb.h; wired into physics.h collision/trace/ground height
# and the lobby renderer). Headless, ASan+UBSan: pure math, then through the real engine paths.
.PHONY: test-obb
test-obb:
	gcc -std=c99 -Wall -Wextra -pedantic -Werror -g -fsanitize=address,undefined apps/tests/test_obb.c -o /tmp/shankpit_test_obb -lm
	/tmp/shankpit_test_obb
	gcc -std=gnu99 -Wall -g -fsanitize=address,undefined $(INCLUDES) \
		packages/simulation/oriented_boxes_test.c packages/world/terrain.c -o /tmp/shankpit_oriented_boxes_test -lm
	/tmp/shankpit_oriented_boxes_test
