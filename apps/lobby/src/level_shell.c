/* apps/lobby/src/level_shell.c -- see level_shell.h. SDL/GL plumbing over packages/wasm_host/level_host. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200112L /* stat, getcwd: save -> compile is a dev-box feature (decision B) */
#endif
#include "level_shell.h"

#include <SDL2/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor_widget_bridge.h"
#include "../../../packages/reflux/reflux_runtime.h"
#include "../../../packages/simulation/level_manager_rules.h"
#include "../../../packages/wasm_host/level_host.h"

#ifndef _WIN32
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define LS_CAN_COMPILE 1
#else
#define LS_CAN_COMPILE 0 /* decision B: compiling levels is a dev-box (Linux) feature in T0 */
#endif

/* main.c's 1280x720 virtual-screen text (packages/ui/turtle_text.h: 5x7 glyphs, 6 * size px a character) */
void draw_string(const char *str, float x, float y, float size);

#define LS_VIRTUAL_W 1280.0f
#define LS_VIRTUAL_H 720.0f
#define LS_BANNER_Y 700.0f /* glyphs hang down from y: 7 rows * size */
/* the level's build manifest, inside the PARENA checkout ($PARENA_DIR, default ../PARENA): its `entry` line is
 * the file view-source opens (contract D11; decision B: source and compile are dev-box features) */
#define LS_MANIFEST "stdlib/brawlpit/level.manifest"

static level_host g_lh;
static int g_inited = 0;
static GLuint g_tex = 0;
static int g_tex_w = 0, g_tex_h = 0;
static int g_have_frame = 0;      /* the texture holds a frame of the current instance */
static SDL_Keycode g_chord_sym = 0; /* a consumed chord's key: its repeats and its key-up are swallowed too */
static int g_buttons = 0;         /* the level's view of the mouse buttons (bit 0 = primary): only presses made
                                   * while it is in front count, so a button held in the world across Shift+M
                                   * never arrives as a click nobody made in the level */
static char g_last_msg[256];
static int g_logged_state = -1;   /* the state last printed: every transition is one stdout line */
static int g_trace = 0;           /* SHANKPIT_LEVEL_TRACE set: print every level-family REFLUX row too */
static int32_t g_trace_cursor = 0;
static double g_last_dt = 1.0 / 60.0; /* the frame time, for the overlay's own tick */
static char g_notice[256];          /* a short-lived line under the hint (e.g. "no source on this machine") */
static Uint32 g_notice_until = 0;

/* ---- save -> compile -> swap (T0 step 6). PARENA decides when (lm-compile-now) and where it swaps in
 * (lm-swap-on); this file watches the manifest's sources, runs PARENA/scripts/build_level_wasm.sh on a thread, and
 * hands the result to level_host_swap. The build writes var/levels/brawlpit_level.wasm (a build output, not a user
 * file: the INV-4.4.1 reading in the contract) and refuses to overwrite it on failure. */
#define LS_MAX_WATCH 48
#define LS_BUILD_OUT "var/levels/brawlpit_level.wasm"
#define LS_BUILD_LOG "var/levels/brawlpit_build.log"
#define LS_BUILD_STATUS "var/levels/brawlpit_build.status" /* the build's own exit code, written by its shell */
#if LS_CAN_COMPILE
static char g_watch[LS_MAX_WATCH][512]; /* the manifest and every `src` it lists */
static long g_watch_mtime[LS_MAX_WATCH];
static int g_nwatch = 0;
static int g_can_build = 0;               /* ../PARENA's build script and the LLVM 18 toolchain are here */
static int g_changed = 0;                 /* a watched file changed since the last build started */
static unsigned long g_watch_hash = 0;    /* the watched files' contents when the last build started (or at boot):
                                           * an mtime change with the same bytes -- the editor's autosave rewriting
                                           * what is on disk -- is not a change */
static Uint32 g_next_watch = 0;
static char g_build_cmd[4096];
#endif
static SDL_atomic_t g_build;              /* 0 idle, 1 building, 2 built, 3 failed */
static char g_built_path[640];            /* the newest good build (absolute) */
static int g_pending_swap = 0;            /* a good build waits for the next entry into the foreground */
static void watch_init(void);
static void watch_tick(void);

static void notice(const char *text) {
    size_t n = strlen(text);
    if (n >= sizeof g_notice) n = sizeof g_notice - 1; /* the screen shows ~100 characters; the log gets all */
    memcpy(g_notice, text, n);
    g_notice[n] = 0;
    g_notice_until = SDL_GetTicks() + 4000;
    fprintf(stderr, "[level] %s\n", text);
}

static int in_source(int st) { return st == lm_st_source() || st == lm_st_source_none(); }

static const char *state_name(int s) {
    static const char *names[] = {"NONE", "LOADING", "FG", "BG", "SOURCE", "SOURCE_NONE"};
    return (s >= 0 && s < 6) ? names[s] : "BOOT";
}

/* one line per transition, and with SHANKPIT_LEVEL_TRACE every lifecycle / BRAWLPIT row as it lands: what
 * scripts/level_smoke_test.sh asserts on, and what a player's log shows when a level misbehaves */
static void note(void) {
    int st = level_host_state(&g_lh);
    if (st != g_logged_state) {
        printf("[level] %s %s -> %s\n", g_lh.desc.name, state_name(g_logged_state), state_name(st));
        g_logged_state = st;
    }
    if (g_trace) {
        int32_t total = reflux_host_total_dispatched(), abs = g_trace_cursor;
        if (abs < reflux_host_oldest_abs()) abs = reflux_host_oldest_abs();
        for (; abs < total; abs++) {
            int i = reflux_host_index_for_abs(abs), t = reflux_host_action_type_at(i);
            if (reflux_action_is_level(t) || reflux_action_is_brawlpit(t))
                printf("[level] reflux %d (%d, %d, %d)\n", t, reflux_host_action_a_at(i), reflux_host_action_b_at(i),
                       reflux_host_action_c_at(i));
        }
        g_trace_cursor = total;
    }
    fflush(stdout);
}

static int is_mac(void) {
#ifdef __APPLE__
    return 1;
#else
    return 0;
#endif
}

void level_shell_init(int boot_level, unsigned int seed) {
    level_desc d;
    memset(&d, 0, sizeof d);
    d.name = "BRAWLPIT";
    d.module_path = "assets/levels/brawlpit/brawlpit_level.wasm";
    d.level_id = REFLUX_LEVEL_BRAWLPIT;
    d.accepts = reflux_action_is_brawlpit;
    d.n_assets = 2;
    d.assets[0].png_path = "assets/levels/brawlpit/sprites.png"; /* 8 portrait tiles, 128x128, 65536 B apart */
    d.assets[0].tile_w = 128;
    d.assets[0].tile_h = 128;
    d.assets[0].stride = 65536;
    d.assets[1].png_path = "assets/levels/brawlpit/duck.png";    /* the 640x340 duck atlas */
    level_host_init(&g_lh, &d, boot_level ? lm_st_loading() : lm_st_none(), (int)(seed & 0x7fffffff));
    g_inited = 1;
    g_last_msg[0] = 0;
    g_trace = getenv("SHANKPIT_LEVEL_TRACE") != NULL;
    g_trace_cursor = reflux_host_total_dispatched();
    g_logged_state = -1;
    SDL_AtomicSet(&g_build, 0);
    g_pending_swap = 0;
    watch_init();
    note();
}

int level_shell_owns_input(void) { return g_inited && level_host_owns_input(&g_lh); }

int level_shell_world_input(void) { return !g_inited || lm_world_input(level_host_state(&g_lh)); }

/* the level's entry source file (the manifest's `entry`, under the PARENA checkout); 1 = it is readable */
static int source_entry(char *out, size_t n) {
    const char *root = getenv("PARENA_DIR");
    char manifest[512], line[512], key[16], val[400];
    FILE *f;
    if (!root || !root[0]) root = "../PARENA"; /* parena_root(), spelled out: this is defined above it */
    snprintf(manifest, sizeof manifest, "%s/%s", root, LS_MANIFEST);
    out[0] = 0;
    f = fopen(manifest, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, "%15s %399s", key, val) == 2 && strcmp(key, "entry") == 0) {
            snprintf(out, n, "%s/%s", root, val);
            break;
        }
    fclose(f);
    if (!out[0] || !(f = fopen(out, "r"))) return 0;
    fclose(f);
    return 1;
}

int level_shell_rejected_dispatches(void) { return g_inited ? level_host_rejected(&g_lh) : 0; }

#if LS_CAN_COMPILE
static const char *parena_root(void) {
    const char *root = getenv("PARENA_DIR");
    return (root && root[0]) ? root : "../PARENA";
}

static long file_mtime(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 ? (long)st.st_mtime : -1;
}

/* FNV-1a over every watched file's bytes */
static unsigned long hash_files(void) {
    unsigned long h = 2166136261UL;
    int i, c;
    for (i = 0; i < g_nwatch; i++) {
        FILE *f = fopen(g_watch[i], "rb");
        h = (h ^ (unsigned long)i) * 16777619UL;
        if (!f) continue;
        while ((c = fgetc(f)) != EOF) h = ((h ^ (unsigned long)c) * 16777619UL) & 0xffffffffUL;
        fclose(f);
    }
    return h;
}

/* the manifest and its `src` files, under the PARENA root; whether this machine can build at all */
static void watch_init(void) {
    const char *tc = getenv("LLVM_TOOLCHAIN_ROOT"), *home = getenv("HOME");
    char path[512], line[512], key[16], val[400];
    FILE *f;
    int i;
    g_nwatch = 0;
    snprintf(path, sizeof path, "%s/%s", parena_root(), LS_MANIFEST);
    f = fopen(path, "r");
    if (!f) return;
    snprintf(g_watch[g_nwatch++], sizeof g_watch[0], "%s", path);
    while (fgets(line, sizeof line, f) && g_nwatch < LS_MAX_WATCH)
        if (sscanf(line, "%15s %399s", key, val) == 2 && strcmp(key, "src") == 0)
            snprintf(g_watch[g_nwatch++], sizeof g_watch[0], "%s/%s", parena_root(), val);
    fclose(f);
    for (i = 0; i < g_nwatch; i++) g_watch_mtime[i] = file_mtime(g_watch[i]);
    g_watch_hash = hash_files();
    if (tc && tc[0]) snprintf(path, sizeof path, "%s/usr/lib/llvm-18/bin/wasm-ld", tc);
    else snprintf(path, sizeof path, "%s/.local/opt/llvm-toolchain/usr/lib/llvm-18/bin/wasm-ld", home ? home : "");
    g_can_build = file_mtime(path) >= 0;
    snprintf(path, sizeof path, "%s/scripts/build_level_wasm.sh", parena_root());
    if (file_mtime(path) < 0) g_can_build = 0;
}

/* The build runs in a child of our own: system() cannot be trusted here. EDITOR.GAME's widget sets SIGCHLD to
 * SIG_IGN (so the editors it spawns reap themselves), after which this process can never wait for a child --
 * system() returned -1 for a build that had succeeded (found by level-smoke). So the child gets SIGCHLD back to
 * the default before exec (its shell must wait for the build script), and the shell writes the build's exit
 * code to a status file this thread polls for. */
static int SDLCALL build_thread(void *unused) {
    FILE *f;
    int code = -1, waited;
    pid_t pid;
    (void)unused;
    remove(LS_BUILD_STATUS);
    pid = fork();
    if (pid == 0) {
        signal(SIGCHLD, SIG_DFL);
        execl("/bin/sh", "sh", "-c", g_build_cmd, (char *)NULL);
        _exit(127);
    }
    if (pid < 0) { SDL_AtomicSet(&g_build, 3); return 0; }
    for (waited = 0; waited < 6000 && code < 0; waited++) { /* up to 10 minutes */
        SDL_Delay(100);
        if ((f = fopen(LS_BUILD_STATUS, "r")) != NULL) {
            if (fscanf(f, "%d", &code) != 1) code = -1;
            fclose(f);
        }
    }
    waitpid(pid, NULL, WNOHANG); /* reaps it when SIGCHLD is not ignored; harmless when it is */
    SDL_AtomicSet(&g_build, code == 0 ? 2 : 3);
    return 0;
}

static void build_start(void) {
    char cwd[512];
    SDL_Thread *t;
    if (!getcwd(cwd, sizeof cwd)) { notice("cannot compile: the working directory is unknown"); return; }
    mkdir("var", 0755);        /* EEXIST is fine */
    mkdir("var/levels", 0755);
    snprintf(g_built_path, sizeof g_built_path, "%s/%s", cwd, LS_BUILD_OUT);
    if (snprintf(g_build_cmd, sizeof g_build_cmd,
                 "'%s/scripts/build_level_wasm.sh' '%s' '%s' > '%s/%s' 2>&1; echo $? > '%s/%s.tmp' && mv '%s/%s.tmp' '%s/%s'",
                 parena_root(), LS_MANIFEST, g_built_path, cwd, LS_BUILD_LOG, cwd, LS_BUILD_STATUS, cwd,
                 LS_BUILD_STATUS, cwd, LS_BUILD_STATUS) >= (int)sizeof g_build_cmd ||
        strchr(parena_root(), '\'') || strchr(cwd, '\'')) {
        notice("cannot compile: the PARENA or working-directory path is too long or contains a quote");
        g_changed = 0;
        return;
    }
    SDL_AtomicSet(&g_build, 1);
    t = SDL_CreateThread(build_thread, "level-build", NULL);
    if (!t) { SDL_AtomicSet(&g_build, 0); notice("cannot compile: no thread"); return; }
    SDL_DetachThread(t);
    g_changed = 0;
    g_watch_hash = hash_files();
    notice("compiling BRAWLPIT from its PARENA source...");
}

/* the build's own located error (build_level_wasm.sh prints "build_level_wasm: compile error at file:line: ...") */
static void build_failed_notice(void) {
    FILE *f = fopen(LS_BUILD_LOG, "r");
    char line[512], text[600];
    const char *found = NULL;
    while (f && fgets(line, sizeof line, f))
        if (strncmp(line, "build_level_wasm:", 17) == 0) { found = line + 18; break; }
    if (f) fclose(f);
    if (found) {
        line[strcspn(line, "\n")] = 0;
        snprintf(text, sizeof text, "compile failed (the running BRAWLPIT is kept): %s", found);
    } else {
        snprintf(text, sizeof text, "compile failed (the running BRAWLPIT is kept): see %s", LS_BUILD_LOG);
    }
    notice(text);
}

/* once a second: did a watched file change; is a build to start or to collect */
static void watch_tick(void) {
    int i, st;
    Uint32 now = SDL_GetTicks();
    st = SDL_AtomicGet(&g_build);
    if (st == 2 || st == 3) {
        SDL_AtomicSet(&g_build, 0);
        if (st == 2) {
            if (lm_loaded(level_host_state(&g_lh))) {
                g_pending_swap = 1;
                notice("built: the new BRAWLPIT swaps in when you go back to it (Escape / Shift+M)");
            } else {
                g_lh.desc.module_path = g_built_path; /* nothing loaded: the next load is the new build */
                notice("built: Shift+M loads the new BRAWLPIT");
            }
        } else {
            build_failed_notice();
        }
    }
    if (!g_nwatch || (Sint32)(now - g_next_watch) < 0) return;
    g_next_watch = now + 1000;
    {
        int touched = 0;
        for (i = 0; i < g_nwatch; i++) {
            long m = file_mtime(g_watch[i]);
            if (m != g_watch_mtime[i]) { g_watch_mtime[i] = m; touched = 1; }
        }
        if (touched && hash_files() != g_watch_hash) g_changed = 1;
    }
    if (g_changed && !g_can_build) {
        g_changed = 0;
        notice("saved -- but this machine cannot compile levels (needs ../PARENA and LLVM 18: MONOREPO BOOTSTRAP.md)");
        return;
    }
    if (lm_compile_now(g_changed, SDL_AtomicGet(&g_build) != 0, g_can_build)) build_start();
}
#else
static void watch_init(void) {}
static void watch_tick(void) {}
#endif

/* after any transition: a visible cursor whenever the world is not the one reading input (the level or the
 * overlay has it); into the overlay, the level's entry file opens in the editor */
static void after_transition(int old) {
    int now = level_host_state(&g_lh);
    static char path[512]; /* static: the editor widget is handed this path (it copies since EDITOR.GAME's fix) */
    if (now == old) return;
    if (lm_world_input(old) && !lm_world_input(now)) SDL_SetRelativeMouseMode(SDL_FALSE);
    /* an entry into the foreground: where a newer build swaps in (PARENA lm-swap-on) */
    if (g_pending_swap && lm_swap_on(old, now)) {
        char text[300];
        g_pending_swap = 0;
        if (level_host_swap(&g_lh, g_built_path)) {
            snprintf(text, sizeof text, "BRAWLPIT rebuilt from source (generation %d)", g_lh.generation);
            g_have_frame = 0;
        } else {
            snprintf(text, sizeof text, "the new build was refused, the old one keeps running: %s", level_host_message(&g_lh));
        }
        notice(text);
    }
    if (lm_owns_input(old) != lm_owns_input(now)) g_buttons = 0;
    if (in_source(now) && !in_source(old) && !(source_entry(path, sizeof path) && editor_widget_overlay_open_file(path))) {
        notice("the source editor could not open (no editor widget in this build)");
        level_host_command(&g_lh, lm_cmd_overlay_closed());
        after_transition(now);
    }
}

/* window pixels -> framebuffer pixels, through the letterboxed viewport */
static void map_pointer(const level_shell_view *v, int wx, int wy, int *fx, int *fy) {
    int fbw = g_lh.fb_w > 0 ? g_lh.fb_w : 960, fbh = g_lh.fb_h > 0 ? g_lh.fb_h : 540;
    int top = v->win_h - v->vp_y - v->vp_h;
    int x = v->vp_w > 0 ? (int)((long)(wx - v->vp_x) * fbw / v->vp_w) : 0;
    int y = v->vp_h > 0 ? (int)((long)(wy - top) * fbh / v->vp_h) : 0;
    *fx = x < 0 ? 0 : x >= fbw ? fbw - 1 : x;
    *fy = y < 0 ? 0 : y >= fbh ? fbh - 1 : y;
}

/* an input event the world must not see while something is in front of it */
static int is_input_event(Uint32 type) {
    return type == SDL_KEYDOWN || type == SDL_KEYUP || type == SDL_TEXTINPUT || type == SDL_TEXTEDITING ||
           type == SDL_MOUSEMOTION || type == SDL_MOUSEBUTTONDOWN || type == SDL_MOUSEBUTTONUP ||
           type == SDL_MOUSEWHEEL || type == SDL_CONTROLLERBUTTONDOWN || type == SDL_CONTROLLERBUTTONUP ||
           type == SDL_CONTROLLERAXISMOTION;
}

int level_shell_event(const SDL_Event *e, int text_focus, const level_shell_view *v) {
    int was_fg, fx, fy, old;
    if (!g_inited || !e) return 0;
    old = level_host_state(&g_lh);
    was_fg = level_host_owns_input(&g_lh);
    /* under the view-source overlay every input event is the editor's (Escape closes it) */
    if (in_source(old) && e->type != SDL_WINDOWEVENT) {
        if (editor_widget_overlay_is_open() && editor_widget_overlay_handle_sdl_event(e)) return 1;
        return is_input_event(e->type);
    }
    switch (e->type) {
    case SDL_KEYDOWN: {
        SDL_Keymod m = (SDL_Keymod)e->key.keysym.mod;
        int shift = (m & KMOD_SHIFT) != 0, ctrl = (m & KMOD_CTRL) != 0, alt = (m & KMOD_ALT) != 0;
        int gui = (m & KMOD_GUI) != 0, rep = e->key.repeat != 0;
        char path[512];
        if (g_chord_sym && e->key.keysym.sym == g_chord_sym && e->key.repeat) return 1;
        /* view-source needs the level's source on this machine (decision B): say so instead of opening nothing */
        if (lm_chord((int)e->key.keysym.sym, shift, ctrl, alt, gui, is_mac(), text_focus, rep) == lm_cmd_source() &&
            !source_entry(path, sizeof path)) {
            notice("BRAWLPIT's source is not on this machine (view-source needs a PARENA checkout at ../PARENA)");
            g_chord_sym = e->key.keysym.sym;
            return 1;
        }
        if (level_host_chord(&g_lh, (int)e->key.keysym.sym, shift, ctrl, alt, gui, is_mac(), text_focus, rep)) {
            g_chord_sym = e->key.keysym.sym;
            after_transition(old);
            note();
            return 1;
        }
        if (!was_fg) return 0;
        level_host_key(&g_lh, (int)e->key.keysym.sym, 1);
        return 1;
    }
    case SDL_KEYUP:
        if (g_chord_sym && e->key.keysym.sym == g_chord_sym) { g_chord_sym = 0; return 1; }
        if (!was_fg) return 0;
        level_host_key(&g_lh, (int)e->key.keysym.sym, 0);
        return 1;
    case SDL_TEXTINPUT:
    case SDL_TEXTEDITING:
        return was_fg; /* the level reads key codes; the world must not get the text */
    case SDL_MOUSEMOTION:
        if (!was_fg || !v) return 0;
        map_pointer(v, e->motion.x, e->motion.y, &fx, &fy);
        level_host_pointer(&g_lh, fx, fy, g_buttons);
        return 1;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        if (!was_fg || !v) return 0;
        if (e->button.button == SDL_BUTTON_LEFT) g_buttons = (e->type == SDL_MOUSEBUTTONDOWN) ? 1 : 0;
        map_pointer(v, e->button.x, e->button.y, &fx, &fy);
        level_host_pointer(&g_lh, fx, fy, g_buttons);
        return 1;
    case SDL_MOUSEWHEEL:
        return was_fg;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
    case SDL_CONTROLLERAXISMOTION:
        return was_fg; /* gamepad is deferred for levels (contract); the world must not get it either */
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) { level_host_blur(&g_lh); g_buttons = 0; }
        return 0;
    default:
        return 0;
    }
}

void level_shell_leave_world(void) {
    int old;
    if (!g_inited) return;
    old = level_host_state(&g_lh);
    if (in_source(old)) editor_widget_overlay_close();
    if (level_host_command(&g_lh, lm_cmd_suspend())) {
        after_transition(old);
        note();
    }
    g_buttons = 0;
    g_chord_sym = 0;
}

void level_shell_frame(double frame_seconds) {
    int old, dt_us;
    const char *msg;
    if (!g_inited) return;
    g_last_dt = frame_seconds > 0.0 ? frame_seconds : 0.0;
    old = level_host_state(&g_lh);
    /* the overlay closed itself (Escape, or the editor's own quit): an entry back into the level, or nothing */
    if (in_source(old) && !editor_widget_overlay_is_open()) {
        level_host_command(&g_lh, lm_cmd_overlay_closed());
        after_transition(old);
        note();
        old = level_host_state(&g_lh);
    }
    watch_tick();
    dt_us = frame_seconds <= 0.0 ? 0 : frame_seconds >= 1.0 ? 1000000 : (int)(frame_seconds * 1.0e6);
    if (old == lm_st_loading()) g_have_frame = 0;
    level_host_frame(&g_lh, dt_us);
    after_transition(old);
    note();
    /* whoever grabbed the mouse meanwhile (a mode start, a menu), it is not the world's while something is in
     * front of it */
    if (!lm_world_input(level_host_state(&g_lh)) && SDL_GetRelativeMouseMode()) SDL_SetRelativeMouseMode(SDL_FALSE);
    msg = level_host_message(&g_lh);
    if (msg[0] && strcmp(msg, g_last_msg) != 0) fprintf(stderr, "[level] %s\n", msg);
    snprintf(g_last_msg, sizeof g_last_msg, "%s", msg);
}

static void begin_2d(void) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, LS_VIRTUAL_W, 0.0, LS_VIRTUAL_H, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
}

static void end_2d(void) {
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopClientAttrib();
    glPopAttrib();
}

static void draw_text_centered(const char *text, float y, float size, float r, float g, float b) {
    char s[112];
    size_t max = (size_t)(LS_VIRTUAL_W / (size * 6.0f)) - 2, n = strlen(text);
    float w;
    if (max >= sizeof s) max = sizeof s - 1;
    if (n > max) snprintf(s, sizeof s, "%.*s...", (int)(max - 3), text);
    else snprintf(s, sizeof s, "%s", text);
    w = (float)strlen(s) * size * 6.0f;
    glColor3f(0.0f, 0.0f, 0.0f);
    draw_string(s, (LS_VIRTUAL_W - w) * 0.5f + size, y - size, size);   /* a drop shadow over a bright world */
    glColor3f(r, g, b);
    draw_string(s, (LS_VIRTUAL_W - w) * 0.5f, y, size);
}

/* the short-lived notice, third line from the top (inside begin_2d) */
static int notice_live(void) { return g_notice[0] && SDL_GetTicks() < g_notice_until; }
static void draw_notice(void) {
    if (notice_live()) draw_text_centered(g_notice, LS_BANNER_Y - 56.0f, 2.0f, 0.9f, 0.9f, 0.95f);
}

void level_shell_draw(const level_shell_view *v) {
    int w = 0, h = 0, dirty = 0, banner, st;
    const uint8_t *fb;
    if (!g_inited) return;
    st = level_host_state(&g_lh);
    if (in_source(st)) {
        /* the view-source overlay (EDITOR.GAME's widget) over the world, the level suspended under it; the
         * bridge maps the mouse over the WHOLE window, so draw it there */
        begin_2d();
        if (v) glViewport(0, 0, v->win_w, v->win_h);
        editor_widget_overlay_tick_and_draw(g_last_dt);
        glDisable(GL_TEXTURE_2D);
        draw_notice();
        end_2d();
        return;
    }
    if (level_host_owns_input(&g_lh)) {
        fb = level_host_fb(&g_lh, &w, &h, &dirty);
        if (!fb) return;
        begin_2d();
        glDisable(GL_BLEND);
        if (v) {
            /* the letterbox map_pointer maps the mouse through, bars black: the world's passes (bloom) may
             * have left the viewport on the whole window */
            glViewport(0, 0, v->win_w, v->win_h);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glViewport(v->vp_x, v->vp_y, v->vp_w, v->vp_h);
        }
        if (!g_tex) glGenTextures(1, &g_tex);
        glBindTexture(GL_TEXTURE_2D, g_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (w != g_tex_w || h != g_tex_h) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, fb);
            g_tex_w = w;
            g_tex_h = h;
        } else if (dirty || !g_have_frame) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, fb);
        }
        g_have_frame = 1;
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS); /* fb row 0 is the top of the picture */
        glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, LS_VIRTUAL_H);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(LS_VIRTUAL_W, LS_VIRTUAL_H);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(LS_VIRTUAL_W, 0.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 0.0f);
        glEnd();
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
        draw_notice();
        end_2d();
        return;
    }
    g_have_frame = 0;
    banner = level_host_banner(&g_lh);
    if (!banner && !level_host_message(&g_lh)[0] && !notice_live()) return;
    begin_2d();
    if (v) glViewport(v->vp_x, v->vp_y, v->vp_w, v->vp_h);
    glDisable(GL_TEXTURE_2D);
    /* top centre: the world's HUD owns the bottom rows and the top-right corner */
    if (banner == 1) draw_text_centered("SHIFT+M RESUME BRAWLPIT   SHIFT+X CLOSE IT", LS_BANNER_Y, 2.5f, 1.0f, 0.85f, 0.2f);
    else if (banner == 2) draw_text_centered("SHIFT+M PLAY BRAWLPIT", LS_BANNER_Y, 2.5f, 0.6f, 0.9f, 1.0f);
    if (level_host_message(&g_lh)[0]) draw_text_centered(level_host_message(&g_lh), LS_BANNER_Y - 28.0f, 2.0f, 1.0f, 0.45f, 0.4f);
    draw_notice();
    end_2d();
}

void level_shell_shutdown(void) {
    if (!g_inited) return;
    level_host_shutdown(&g_lh);
    if (g_tex) glDeleteTextures(1, &g_tex);
    g_tex = 0;
    g_tex_w = g_tex_h = 0;
    g_inited = 0;
}
