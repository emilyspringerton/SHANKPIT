# mk/verdict_fx.mk -- the civic boss's verdict effect pools (packages/render/verdict_fx.h).
#
# test-verdict-fx exists because these pools used to be file-static state inside apps/lobby, where the
# only way to see whether an ember expired, whether the ring wrapped, or whether a scorch ever faded was
# to watch a screen -- and this build has no display. The header is GL-free on purpose, so the same
# spawn/sample/age logic the renderer runs is exercised here with no context at all.
#
# What this does NOT cover (stated, not glossed): the shader itself -- the ring, the ember sprite and the
# scorch pattern -- is drawn by apps/lobby and needs a real GL 2.0 context. This tests the state that
# shader is fed.

VERDICT_FX_TEST := $(BIN_DIR)/verdict_fx_test

.PHONY: test-verdict-fx
test-verdict-fx: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror \
		-Ipackages/render -Ipackages/simulation \
		packages/render/verdict_fx_test.c \
		-o $(VERDICT_FX_TEST) $(LIBS_M)
	./$(VERDICT_FX_TEST)
