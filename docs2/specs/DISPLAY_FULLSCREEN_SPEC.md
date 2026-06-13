# Display, Fullscreen & Resolution — Pre-Steam Spec

*Status: REQUIRED — blocks Milestone 6 (Steam Direct launch)*
*Author: Emily Prime via Claude Code, 2026-06-13*

---

## Why This Is a Hard Steam Blocker

SHANKPIT's client is hardcoded to a 1280×720 windowed window with no fullscreen capability
and no resolution picker. Steam reviewers play in fullscreen. Most PC gamers default to their
monitor's native resolution (usually 1080p or higher). Shipping without fullscreen means:

- Every player on a 1080p+ monitor gets a small 1280×720 window floating in the center of
  their screen on first launch — the worst first impression possible.
- The Steam store page will be rejected or flagged during review if basic display expectations
  aren't met.
- Steam Deck (a likely target) requires proper fullscreen behavior for the primary UX mode.

**This must land before the Steam Direct submission.**

---

## Current State Audit

All violations in `apps/lobby/src/main.c`:

### Window creation (line 6359)
```c
SDL_Window *win = SDL_CreateWindow("SHANKPIT", 100, 100, 1280, 720, SDL_WINDOW_OPENGL);
```
Hardcoded 1280×720. No `SDL_WINDOW_RESIZABLE`. No fullscreen flag.

### 3D projection (line 1355)
```c
gluPerspective(75.0, 1280.0/720.0, 0.1, Z_FAR);
```
Hardcoded aspect ratio. Will distort on any non-16:9 resolution or fullscreen at 1080p.

### 2D / HUD projections — 6 hardcoded sites
| Line | Call |
|------|------|
| 958 | `gluOrtho2D(0, 1280, 0, 720)` |
| 4385 | `gluOrtho2D(0, 1280, 0, 720)` |
| 4682 | `gluOrtho2D(0, 1280, 0, 720)` |
| 5033 | `gluOrtho2D(0, 1280, 0, 720)` |
| 4931 | `glOrtho(0, 1280, 0, 720, -1, 1)` |
| 5018 | `glOrtho(0, 1280, 0, 720, -1, 1)` |

All HUD, overlay, and UI geometry is authored in 1280×720 virtual coordinates.

### F11 key (line 6586–6588)
F11 is currently taken by `voxworld_points_debug`. Alt+Enter must be the fullscreen toggle.

### Mouse coordinate assumption
The lobby UI click regions (LOBBYMENU, skin picker, text input boxes) assume the mouse is
operating in 1280×720 space. This needs remapping if the window is a different size.

---

## Recommended Approach: Virtual Canvas + Borderless Fullscreen

### Design principle
Do NOT refactor all 1280×720 HUD coordinates. Instead, establish a permanent virtual canvas
of 1280×720 that the entire 2D pipeline draws into, and use `glViewport` to map that canvas
to the actual display with correct aspect-ratio letterboxing.

This is the minimum-risk approach for EA: zero change to existing UI code, the retro aesthetic
is preserved at its authored scale, and the fix is isolated to the display setup path.

### Virtual canvas letterbox calculation
At the start of each frame (and on every `SDL_WINDOWEVENT_RESIZED` event):

```c
#define VIRTUAL_W 1280
#define VIRTUAL_H 720

static int g_win_w = 1280, g_win_h = 720;
static int g_vp_x, g_vp_y, g_vp_w, g_vp_h;

static void recalc_viewport(void) {
    float target = (float)VIRTUAL_W / (float)VIRTUAL_H;  /* 16:9 */
    float actual = (float)g_win_w / (float)g_win_h;
    if (actual > target) {
        /* Pillarbox — window is wider than 16:9 */
        g_vp_h = g_win_h;
        g_vp_w = (int)(g_win_h * target);
        g_vp_x = (g_win_w - g_vp_w) / 2;
        g_vp_y = 0;
    } else {
        /* Letterbox — window is narrower than 16:9 */
        g_vp_w = g_win_w;
        g_vp_h = (int)(g_win_w / target);
        g_vp_x = 0;
        g_vp_y = (g_win_h - g_vp_h) / 2;
    }
}
```

Before any draw: `glViewport(g_vp_x, g_vp_y, g_vp_w, g_vp_h)`.
Then clear with a black fill covering the full window (bars are black).

### 3D projection fix
Replace the hardcoded aspect ratio:
```c
/* Before */
gluPerspective(75.0, 1280.0/720.0, 0.1, Z_FAR);

/* After */
gluPerspective(current_fov, (float)VIRTUAL_W / (float)VIRTUAL_H, 0.1, Z_FAR);
```
The virtual canvas ratio stays 16:9 regardless of actual window size — the viewport handles
the physical mapping. FOV stays correct on all displays.

### 2D projections — no change needed
Because the viewport is already mapped so that 1280×720 virtual fills the glViewport region,
all existing `gluOrtho2D(0, 1280, 0, 720)` calls continue to work correctly with zero edits.

### Mouse coordinate remapping (lobby UI only)
Lobby click regions compare mouse XY against virtual 1280×720 coordinates. SDL gives mouse
position in actual window space. Remap on every mouse event:

```c
static void remap_mouse(int wx, int wy, int *vx, int *vy) {
    *vx = (int)((float)(wx - g_vp_x) / g_vp_w * VIRTUAL_W);
    *vy = (int)((float)(g_win_h - wy - g_vp_y) / g_vp_h * VIRTUAL_H);
}
```

Apply this to `SDL_MOUSEBUTTONDOWN`, `SDL_MOUSEBUTTONUP`, `SDL_MOUSEMOTION` before any
existing lobby click-region comparisons.

---

## Fullscreen Mode

### Strategy: borderless fullscreen only (for EA)
Use `SDL_WINDOW_FULLSCREEN_DESKTOP` exclusively. Do NOT use `SDL_WINDOW_FULLSCREEN`
(exclusive fullscreen). Reasons:
- Exclusive fullscreen breaks the Steam overlay on some configurations.
- `FULLSCREEN_DESKTOP` uses the monitor's native resolution, avoiding mode switching
  artifacts and the display flicker that destroys first-impressions.
- It is the de-facto standard for modern indie PC games on Steam.
- Windowed mode remains available for multi-monitor setups and streamers.

### Toggle
Alt+Enter (`SDLK_RETURN` + `SDL_Keymod & KMOD_ALT`) in the game input handler.
Also expose a "Fullscreen" toggle in the lobby settings UI (future sprint — not EA-blocking).

```c
static int g_fullscreen = 0;
static SDL_Window *g_win = NULL;

static void toggle_fullscreen(void) {
    g_fullscreen = !g_fullscreen;
    SDL_SetWindowFullscreen(g_win, g_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    SDL_GetWindowSize(g_win, &g_win_w, &g_win_h);
    recalc_viewport();
    save_display_config();
}
```

### On startup
Check `display.cfg` and apply stored fullscreen preference before the first frame.

---

## Configuration: display.cfg

Add a new config file alongside the existing `shankpit_skin.cfg`:

```
# SHANKPIT display config
fullscreen=1
win_w=1280
win_h=720
```

- `fullscreen=1` triggers borderless fullscreen on launch.
- `win_w` / `win_h` are the windowed-mode size, persisted for when the player leaves
  fullscreen. Default: 1280×720.
- File is read at startup, written on any change (toggle fullscreen, resize).

### Supported windowed resolutions (for a future resolution picker)
Common 16:9 resolutions the UI should offer. Not required for EA, but define the list now:

| Label | Width | Height |
|-------|-------|--------|
| 1280×720 (default) | 1280 | 720 |
| 1600×900 | 1600 | 900 |
| 1920×1080 | 1920 | 1080 |
| 2560×1440 | 2560 | 1440 |

Ultrawide (21:9) players will get pillarboxing in windowed mode, which is acceptable for EA.
Post-launch can add a wide-canvas mode that stretches the virtual canvas horizontally.

---

## Engineering Checklist (implementation order)

All work in `apps/lobby/src/main.c` unless noted.

- [ ] **Globals** — Add `g_win_w`, `g_win_h`, `g_vp_x`, `g_vp_y`, `g_vp_w`, `g_vp_h`,
  `g_fullscreen`, and the `g_win` pointer at top of file (near `current_fov` at line 112).
- [ ] **`recalc_viewport()`** — Add the virtual canvas letterbox function.
- [ ] **Window creation** — Add `SDL_WINDOW_RESIZABLE` flag (line 6359). Store the
  `SDL_Window*` in `g_win`. Call `recalc_viewport()` immediately after.
- [ ] **Display config** — Add `load_display_config()` and `save_display_config()` alongside
  the existing skin config functions. Load on startup; apply fullscreen flag before first draw.
- [ ] **`SDL_WINDOWEVENT_RESIZED` handler** — In the event loop, update `g_win_w`/`g_win_h`
  and call `recalc_viewport()`. Save display config.
- [ ] **Alt+Enter handler** — In the in-game `SDL_KEYDOWN` block, detect
  `SDLK_RETURN + KMOD_ALT` and call `toggle_fullscreen()`. Add same in lobby keydown block.
- [ ] **glViewport calls** — Add `glViewport(g_vp_x, g_vp_y, g_vp_w, g_vp_h)` at the top
  of the main draw path (before `glClear`). Black-fill the full window for bars.
- [ ] **3D projection** — Replace `1280.0/720.0` with `(float)VIRTUAL_W/(float)VIRTUAL_H`
  at line 1355 (and any other perspective calls added in future).
- [ ] **Mouse remapping** — Apply `remap_mouse()` to all lobby mouse event coordinates
  before they reach click-region comparisons.
- [ ] **Steam Deck** — The Deck runs at 1280×800 (16:10). Virtual canvas will letterbox to
  1280×720, leaving a 40px bar top and bottom. Acceptable for EA. Post-launch: add
  optional 1280×800 virtual canvas mode.

---

## Acceptance Criteria (required before Steam submission)

- [ ] Game launches in fullscreen by default (first launch = fullscreen).
- [ ] Alt+Enter toggles between fullscreen and windowed cleanly, no corruption.
- [ ] In fullscreen at 1080p (1920×1080), the game renders correctly with no distortion —
  black bars top/bottom (40px each side) with correct 16:9 image.
- [ ] In windowed mode at 1280×720, 1600×900, and 1920×1080, game looks identical to current.
- [ ] All HUD elements (crosshair, health bar, ammo, kill feed) render at correct screen
  positions in all tested resolutions.
- [ ] Lobby click regions (menu items, skin picker, connect button) work correctly in all
  tested resolutions.
- [ ] Display preference survives restart (fullscreen=1 persists in display.cfg).
- [ ] Steam overlay (Shift+Tab) works correctly in fullscreen mode.
- [ ] No regression in mouse sensitivity or relative mouse capture.

---

## What This Does NOT Need to Do for EA

- Resolution picker in the lobby UI (fullscreen-only is fine for EA).
- Exclusive fullscreen / refresh-rate selection.
- Ultrawide native support (pillarboxing is acceptable).
- 4K textures or any render-quality scaling (the retro aesthetic looks great at any res).

---

## Related Files

- `apps/lobby/src/main.c` — all implementation lives here
- `docs2/NORTHSTAR.md` — Milestone 5 EA build checklist (this spec added as dependency)
- `CHANGELOG.md` — log implementation when it lands
