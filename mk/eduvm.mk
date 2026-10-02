# EduVM / Architect's Orb (card #494): packages/education/ is vendored from GoblinFoxDragon's education
# package (the GFD/SHANKPIT lineage); eduvm_mod.c is GENERATED from PARENA/stdlib/shankpit/eduvm.prn.
#   make test-eduvm        build + run the mod-over-VM test (gcc, -Werror)
#   make regen-eduvm       regenerate eduvm_mod.c from a sibling ../PARENA checkout
#   make check-eduvm       fail if the checked-in generated file is stale vs the .prn
PARENA_DIR ?= ../PARENA
EDUVM_PRN := $(PARENA_DIR)/stdlib/shankpit/eduvm.prn
EDU_SRC := packages/education/edu_lexer.c packages/education/edu_parser.c packages/education/edu_bytecode.c packages/education/edu_vm.c packages/education/edu_bindings.c packages/education/edu_script.c packages/education/eduvm_host.c packages/education/eduvm_mod_unit.c packages/world/parena_runtime.c

.PHONY: test-eduvm regen-eduvm check-eduvm
test-eduvm:
	gcc -Wall -Wextra -Werror -Wno-unused-parameter -Ipackages/education -Ipackages/world packages/education/eduvm_test.c $(EDU_SRC) -o /tmp/eduvm_test -lm
	/tmp/eduvm_test

regen-eduvm:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(EDUVM_PRN) -o packages/education/eduvm_mod.c

check-eduvm:
	@test -x $(PARENA_DIR)/parena || $(MAKE) -C $(PARENA_DIR) build >/dev/null
	$(PARENA_DIR)/parena build $(EDUVM_PRN) -o /tmp/eduvm_mod.regen.c >/dev/null
	cmp /tmp/eduvm_mod.regen.c packages/education/eduvm_mod.c
