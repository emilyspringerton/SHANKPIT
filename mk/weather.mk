# mk/weather.mk -- what a storm SOUNDS like and what its wind does to the fog (2026-11-05).
#
# The numbers live in PARENA/stdlib/shankpit/weather_audio_rules.prn; packages/audio/wind_audio_rules.c is
# GENERATED from it. The ambience, the fog thinning and the pan all read those same curves, so a gust that
# raises the wind also thins the scud the renderer pools on the ground -- picture and sound cannot drift.
#
#   make regen-wind-audio-rules   regenerate the C from a sibling ../PARENA checkout
#   make check-wind-audio-rules   fail if the checked-in generated file is stale vs the .prn
#   make test-wind-audio          the curves, headless (no device)
#   make test-line-of-sight       the segment-vs-box query, headless
PARENA_DIR ?= ../PARENA
WIND_AUDIO_PRN := $(PARENA_DIR)/stdlib/shankpit/weather_audio_rules.prn

.PHONY: regen-wind-audio-rules check-wind-audio-rules test-wind-audio test-line-of-sight

regen-wind-audio-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(WIND_AUDIO_PRN) -o packages/audio/wind_audio_rules.c

check-wind-audio-rules:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(WIND_AUDIO_PRN) -o /tmp/wind_audio_rules.regen.c >/dev/null
	cmp /tmp/wind_audio_rules.regen.c packages/audio/wind_audio_rules.c

test-wind-audio: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/audio -Ipackages/world \
		packages/audio/wind_audio_test.c packages/audio/wind_audio_rules.c \
		-o $(BIN_DIR)/wind_audio_test $(LIBS_M)
	./$(BIN_DIR)/wind_audio_test

# test-line-of-sight -- the query body ONLY. The behaviour that consumes it (a clerk that cannot see
# through the courthouse, a scorch that cannot burn through a bench) is test-municipal-defense's job.
test-line-of-sight: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation \
		packages/simulation/sight_test.c -o $(BIN_DIR)/sight_test $(LIBS_M)
	./$(BIN_DIR)/sight_test
