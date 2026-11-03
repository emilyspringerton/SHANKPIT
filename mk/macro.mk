# MACROS: packages/ui/macro.h is the FORMAT (parse shankpit_macros.cfg, find a bind, split a body on
# ';', write it back); what a command DOES lives in the client. No Lua, no evaluator, no allocation.
#   make test-macros   headless: comments, malformed lines, rebind replace, ';' split, roundtrip, bounds
.PHONY: test-macros

test-macros: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/ui packages/ui/macro_test.c -o $(BIN_DIR)/macro_test
	./$(BIN_DIR)/macro_test

# WOW-STYLE MACROS: the conditional compiler, the host contract and the .toc loader. Headless and
# dependency-free -- no Lua VM is vendored, so what is tested here is the layer a binding would wrap:
#   make test-wow-macros   text -> bitfield clauses, first-match-wins, tokens, every rejection code,
#                          macro -> host vtable dispatch, the API contract table, .toc parse + order
.PHONY: test-wow-macros

test-wow-macros: | $(BIN_DIR)
	$(CC) -O1 -Wall -Wextra -Werror -Ipackages/ui packages/ui/wow_macro_test.c -o $(BIN_DIR)/wow_macro_test
	./$(BIN_DIR)/wow_macro_test

# The /cast name table is GENERATED from packages/common/protocol.h, so a new weapon cannot exist in
# the engine and be unknown to a macro (or worse, mean a different id).
.PHONY: regen-wow-weapons check-wow-weapons

regen-wow-weapons:
	python3 scripts/gen_wow_weapons.py

check-wow-weapons:
	python3 scripts/gen_wow_weapons.py --check
