# SHANKPIT broadcast cameras (cards #456-#458): packages/render/camera_rules.c is GENERATED from
# PARENA/stdlib/shankpit/camera_rules.prn; camera_rig.h is the rig/director/serialization plumbing.
#   make test-camera-rig     headless rig + director + serialization test (ASan/UBSan)
#   make regen-camera-rules  regenerate camera_rules.c from a sibling ../PARENA checkout
#   make check-camera-rules  fail if the checked-in generated file is stale vs the .prn
PARENA_DIR ?= ../PARENA
CAMERA_PRN := $(PARENA_DIR)/stdlib/shankpit/camera_rules.prn

.PHONY: test-camera-rig test-stream-out regen-camera-rules check-camera-rules
test-camera-rig:
	gcc -std=gnu99 -Wall -Wextra -g -fsanitize=address,undefined $(INCLUDES) packages/render/camera_rig_test.c \
		packages/render/camera_rules.c packages/world/parena_runtime.c -o /tmp/shankpit_camera_rig_test -lm
	/tmp/shankpit_camera_rig_test

test-stream-out:
	gcc -std=gnu99 -Wall -Wextra -g -fsanitize=address,undefined $(INCLUDES) packages/render/stream_out_test.c \
		-o /tmp/shankpit_stream_out_test
	/tmp/shankpit_stream_out_test

regen-camera-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(CAMERA_PRN) -o packages/render/camera_rules.c

check-camera-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(CAMERA_PRN) -o /tmp/camera_rules.regen.c >/dev/null
	cmp /tmp/camera_rules.regen.c packages/render/camera_rules.c
