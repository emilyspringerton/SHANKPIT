# Trees + wood in the zombie modes (packages/simulation/trees.c; rules: PARENA stdlib/shankpit/tree_rules.prn -> tree_rules.c,
# regenerate with scripts/gen_tree_rules.sh). Headless.
.PHONY: test-trees
test-trees:
	gcc -std=c99 -Wall -Wextra -Werror -Ipackages/simulation -Ipackages/world -o /tmp/trees_test packages/simulation/trees_test.c \
	  packages/simulation/trees.c packages/simulation/tree_rules.c packages/world/parena_runtime.c -lm
	/tmp/trees_test
