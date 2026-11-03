# The procedural world textures (packages/render/proc_tex.c). Headless: every generator only fills a
# pixel buffer, so no GL context and no xvfb are needed -- proc_tex_create (whose first act is
# glGenTextures) is deliberately not called by the test. What it pins: same seed -> same pixels,
# different seed -> different pixels, fully opaque, a real tonal range, and a right edge that wraps no
# harder than an ordinary neighbouring column (a visible seam at every box boundary otherwise).
# make test-proc-tex
.PHONY: test-proc-tex
test-proc-tex:
	gcc -std=gnu99 -Wall -Wextra -g -fsanitize=address,undefined $(INCLUDES) \
		packages/render/proc_tex_test.c packages/render/proc_tex.c \
		-o /tmp/shankpit_proc_tex_test -lSDL2 -lGL -lm
	/tmp/shankpit_proc_tex_test
