/* phone_browser_embed.h -- the real in-game PELLEGRINO browser, embedded in the zombies-mode phone
 * (BP_APP_PELLEGRINO, packages/common/phone.h). Unlike packages/pellegrino_embed (the SHANKPIT OS menu,
 * which paints a host-supplied static page and never navigates), this surface is a REAL browser: it
 * calls PELLEGRINO's actual navigation machinery (TLS 1.2/1.3 via mbedTLS, HTTP/3 over QUIC, chain +
 * hostname verification always on, no bypass) and renders PELLEGRINO's OWN page/stream buffer.
 *
 * Flow: the host creates the offscreen surface (phone_browser_open), tells it where to go
 * (phone_browser_navigate), and every frame the app is open calls phone_browser_step to advance any
 * in-flight fetch and repaint -> ARGB8888 pixels the host uploads however it likes (same convention
 * os_menu_draw's pixel buffer already establishes). Mouse/keys/text are forwarded as they happen; link
 * clicks and typed URLs navigate for real, inside this module -- the host never interprets an href
 * itself (unlike the OS menu's `shankpit:go/<n>` pseudo-links). Nothing here touches SHANKPIT's own
 * game state; it is a pure embedded browser. */
#ifndef SHANKPIT_PHONE_BROWSER_EMBED_H
#define SHANKPIT_PHONE_BROWSER_EMBED_H

#ifdef __cplusplus
extern "C" {
#endif

/* Create the offscreen surface renderer (w*h) and reset browser state. SDL video must already be
 * initialised. 0 = ok, -1 = failed. Safe to call again after phone_browser_close. */
int phone_browser_open(int w, int h);

/* Tear down the surface renderer so a later phone_browser_open starts clean (e.g. leaving the app). */
int phone_browser_close(void);

/* Start a real navigation to `url` (http://, https://, or h3://) -- drops any in-flight fetch first,
 * same as typing a URL in the real PELLEGRINO address bar. */
int phone_browser_navigate(const char *url);

/* Advance one frame: pumps any in-flight TLS/HTTP/HTTP-3 fetch (non-blocking), repaints, and resolves
 * any link click/activation into a real navigation. Call every frame the app is open and visible, even
 * with no new input, so a slow fetch keeps progressing. Returns the pixel base (w*h ARGB8888, pitch
 * w*4), or NULL if phone_browser_open has not succeeded. */
const unsigned int *phone_browser_step(int win_w, int win_h);

/* Left click at surface pixel (x,y), origin top-left. */
int phone_browser_click(int x, int y);

/* Mouse wheel, +1/-1 per notch (matches SDL_MOUSEWHEEL's own sign convention). */
int phone_browser_wheel(int delta);

/* One printable ASCII character typed into the address bar or a focused form field. */
int phone_browser_text(char c);

/* SDL keycode. Returns 1 for Esc/q ("quit" in the standalone CLI) -- the host's cue to close this app
 * and return to the phone's home grid; this call never exits the process itself. */
int phone_browser_key(int sdl_keycode);

#ifdef __cplusplus
}
#endif
#endif
