/* phone_chromium.h -- STOPGAP: the SHANKPHONE's web games, run in a headless Chromium (founder, 2026-11-14: "bring in
 * chromium to fill the gaps", answered "Yes, stopgap").
 *
 * Pellegrino has no JavaScript, and a WOTAN web game (shankpit-460, D2, REDGARDEN, Bedrock Racers) is a page that
 * starts its own WebAssembly with JavaScript. Until Pellegrino can run a page's WebAssembly itself (the PARENA
 * replacement, EMILY/BACKLOG.md), the phone starts chromedriver, opens the page in headless Chromium, and plays it
 * through PARENA's own W3C WebDriver client (PARENA stdlib/net/webdriver.prn): a screenshot every arcade-shot-ms
 * (decoded with PARENA's base64.prn + image/png.prn), keys sent back as WebDriver key actions. Slow -- a few frames a
 * second -- and said so on screen. Linux/macOS only (process + thread); on Windows phc_available() is 0.
 *
 * All the WebDriver work happens on one worker thread, so the game loop never waits on the browser. Find the
 * programs with SHANKPIT_CHROMEDRIVER / SHANKPIT_CHROMIUM, else `chromedriver` on PATH and Chromium wherever
 * chromedriver finds it. */
#ifndef SHANKPIT_PHONE_CHROMIUM_H
#define SHANKPIT_PHONE_CHROMIUM_H
#include <stdint.h>

/* 1 when a chromedriver can be found (checked once, then remembered). */
int phc_available(void);
/* Open `url` (stops any page already open). Returns at once; the worker does the rest. */
void phc_open(const char *url);
/* An SDL key while the page plays (down 1 / up 0). Queued; the worker sends it. */
void phc_key(int sym, int down);
/* Copy the latest picture (RGBA, row 0 at the top) into dst if it holds w*h*4 bytes and is newer than *seq.
   Returns 1 and updates *w, *h, *seq when it copied. */
int phc_frame(uint8_t *dst, int cap, int *w, int *h, int *seq);
/* What the bridge is doing, for the screen: "STARTING CHROMEDRIVER", "OPENING THE PAGE", "" when playing, or why it
   stopped. Copied into buf. */
void phc_status(char *buf, int cap);
/* Close the page, end the session, stop chromedriver. Waits for the worker. */
void phc_close(void);

#endif
