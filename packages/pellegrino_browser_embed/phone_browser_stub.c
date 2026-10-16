/* Link stub for targets that cannot build the real embed (Windows/mingw: the embed needs POSIX
 * sockets + a vendored mbedTLS checkout). Every call reports "unavailable"; the phone's
 * PELLEGRINO tile then shows its normal failed-to-open state instead of the build breaking. */
#include "phone_browser_embed.h"
int phone_browser_open(int w, int h) { (void)w; (void)h; return -1; }
int phone_browser_close(void) { return 0; }
int phone_browser_navigate(const char *url) { (void)url; return -1; }
const unsigned int *phone_browser_step(int win_w, int win_h) { (void)win_w; (void)win_h; return 0; }
int phone_browser_click(int x, int y) { (void)x; (void)y; return 0; }
int phone_browser_wheel(int delta) { (void)delta; return 0; }
int phone_browser_text(char c) { (void)c; return 0; }
int phone_browser_key(int sdl_keycode) { (void)sdl_keycode; return 0; }
