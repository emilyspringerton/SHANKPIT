# MODE_TYLER voice (TYLER VALHANNA cold open) -- headless tests: no SDL, no window, no audio device.
# Sources are the server's own list minus its main.c, so a new packages/*.c added to SERVER_SRC is
# picked up here too (the same drift-proofing the CI source lists got). Run from the repo root: the
# tests read assets/tyler_levels/*.json and assets/tyler_vo/*.wav.
TYLER_VO_SRC := $(filter-out apps/server/src/main.c,$(SERVER_SRC))

.PHONY: test-tyler-vo
test-tyler-vo:
	gcc -std=gnu99 -Wall -g -fsanitize=address,undefined $(INCLUDES) -Ipackages/audio \
		tests/tyler/test_tyler_vo.c $(TYLER_VO_SRC) \
		-o /tmp/shankpit_test_tyler_vo -lm
	/tmp/shankpit_test_tyler_vo
