/* os_menu_embed.h -- SHANKPIT OS menu rendered as HTML + CSS through PELLEGRINO's layout/paint engine
 * (kanban #627). PELLEGRINO is PARENA; this header is the whole C surface -- no PARENA type crosses it.
 *
 * Flow: the host builds the page (os_menu_build_page), draws it (os_menu_draw -> ARGB8888 pixels it uploads
 * however it likes), forwards mouse/keys (os_menu_click / os_menu_key), and reads back which link was
 * activated (os_menu_action -> "shankpit:go/<index>" or "shankpit:back"). Nothing here touches the network or
 * opens a window; PELLEGRINO's embed mode never navigates. */
#ifndef SHANKPIT_OS_MENU_EMBED_H
#define SHANKPIT_OS_MENU_EMBED_H

#ifdef __cplusplus
extern "C" {
#endif

#define OS_MENU_PAGE_MAX 16384

/* Create the offscreen surface renderer (w*h). SDL video must already be initialised. 0 = ok, -1 = failed. */
int os_menu_init(int w, int h);

/* Compose the menu document: n tiles with labels[i], laid out in 2 columns, tile `selected` highlighted
 * (-1 = none). Each tile is <a href="shankpit:go/i">. Writes NUL-terminated HTML into out (cap bytes).
 * Returns the length, or -1 if it does not fit. Pure function of its arguments. */
int os_menu_build_page(char *out, int cap, const char *const *labels, int n, int selected);

/* Install `html` as the current page (copied; no-op + 0 if identical to the current one, 1 if it changed). */
int os_menu_set_page(const char *html);

/* Repaint if the page changed since the last draw. Returns the pixel base (w*h ARGB8888, pitch w*4), or NULL
 * if os_menu_init has not succeeded. Cheap when nothing changed. */
const unsigned int *os_menu_draw(void);

/* Left click at surface pixel (x,y), origin top-left. Returns 1 if it landed on a link (see os_menu_action). */
int os_menu_click(int x, int y);

/* SDL keycode: Tab / Shift+Tab move link selection, Return / Space activate it. Returns 1 if an action is ready. */
int os_menu_key(int sdl_keycode);

/* Pop the pending activated link into out ("shankpit:go/3"). Returns 1 and clears it, or 0 if none. */
int os_menu_action(char *out, int cap);

/* Parse "shankpit:go/<n>" -> n, "shankpit:back" -> -2; anything else -> -1. */
int os_menu_parse_action(const char *href);

#ifdef __cplusplus
}
#endif
#endif
