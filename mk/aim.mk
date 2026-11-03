# AIM: the inverse of tp_dir_from_yaw_pitch, checked against the forward formula.
#   make test-aim   plain + ASan/UBSan
test-aim:
	gcc -std=gnu99 -Wall -g -fsanitize=address,undefined $(INCLUDES) \
		packages/common/aim_test.c packages/world/terrain.c \
		-o /tmp/shankpit_aim_test -lm
	/tmp/shankpit_aim_test
