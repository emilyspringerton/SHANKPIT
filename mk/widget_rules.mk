# mk/widget_rules.mk -- composable widget circuits (2026-10-31).
#
# Founder: "the widgets need to be composible ... think light switches, basic circuits, an elevator is a
# widget of widgets ... bring in edu vm script it all needs to play nice with that interpreter".
# The circuit's rules live in PARENA; packages/simulation/widget_circuit.h runs them and mirrors every
# node's value into an EduVM register.

.PHONY: regen-widget-rules test-widget-circuit test-widget-runtime

regen-widget-rules:
	scripts/gen_widget_rules.sh

# test-widget-circuit -- compositions, not single behaviours: lever -> delay -> motor, plate -> transistor
# -> door (a real AND), the register mirror a script reads, the F prompt's proximity/hysteresis, and a
# panel that dials a setpoint without swallowing keys once closed.
test-widget-circuit: | $(BIN_DIR)
	# gnu99, not c99: level_boxes.h uses POSIX popen/pclose (the level fetch helpers), which a strict
	# c99 build hides -- every other level test in this repo builds gnu99 for the same reason.
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation -Ipackages/world \
		packages/simulation/widget_circuit_test.c packages/simulation/widget_rules.c \
		-o $(BIN_DIR)/widget_circuit_test $(LIBS_M)
	./$(BIN_DIR)/widget_circuit_test

# test-widget-runtime -- the host, not the maths: a level's plates fed by live players, a lever -> relay
# -> lift that moves real geometry late, a plate -> transistor -> door that is a real AND, a terminal
# whose dialled setpoint survives the tick, the register mirror firing once per change, and a prompt that
# a busy host suppresses and a player walking away releases.
test-widget-runtime: | $(BIN_DIR)
	$(CC) -std=gnu99 -O1 -Wall -Wextra -Werror -Ipackages/simulation -Ipackages/world \
		packages/simulation/widget_runtime_test.c packages/simulation/widget_rules.c \
		-o $(BIN_DIR)/widget_runtime_test $(LIBS_M)
	./$(BIN_DIR)/widget_runtime_test
