#ifndef SHANKPIT_KEYBIND_HOST_H
#define SHANKPIT_KEYBIND_HOST_H
/* keybind_host.h -- the host side of WoW-style key bindings. Decisions (registry, defaults, key encoding,
 * match, shadowing, conflicts) are PARENA: PARENA/stdlib/shankpit/keybind_rules.prn -> keybind_rules.c
 * (`scripts/gen_keybind_rules.sh`). This file only owns the table (op x 3 slots), the steal loop,
 * the poll/event lookups and the persistence format. SDL-free: scancodes and modifier bits are plain ints
 * (mods: 1 Shift, 2 Ctrl, 4 Alt) so it unit-tests headless. */

#define KB_MAX_OPS 33        /* ids 1..32, index 0 unused */
#define KB_SLOTS 3
#define KB_NUM_SCANCODES 512 /* SDL_NUM_SCANCODES */

/* operation ids -- append-only, persisted (mirror the registry in keybind_rules.prn) */
enum {
    KB_FWD = 1, KB_BACK, KB_LEFT, KB_RIGHT, KB_JUMP, KB_CROUCH, KB_BOOST,
    KB_RELOAD, KB_ABILITY, KB_PHEROMONE,
    KB_INTERACT, KB_HELI_LEFT, KB_HELI_RIGHT, KB_THROTTLE_UP, KB_THROTTLE_DOWN, KB_FLAPS, KB_WHEEL_BRAKE,
    KB_CAMERA_TOGGLE, KB_OVERLAY_MODE,
    KB_AB_1 = 20, /* .. KB_AB_10 = 29 */
    KB_AB_SHIFT = 30, KB_AB_CTRL, KB_AB_ALT
};
#define KB_AB(n) (KB_AB_1 + (n) - 1)

/* contexts */
#define KB_CTX_FOOT 1
#define KB_CTX_VEHICLE 2
#define KB_CTX_PLANE 4
#define KB_CTX_HELI 8

/* PARENA-generated (keybind_rules.c) */
int kb_op_count(void);
int kb_op_valid(int op);
int kb_op_category(int op);
int kb_op_ctx(int op);
int kb_default(int op, int slot);
int kb_key_make(int scan, int mods);
int kb_key_scan(int key);
int kb_key_mods(int key);
int kb_key_valid(int key);
int kb_bindable(int key);
int kb_match(int bind, int scan, int mods);
int kb_shadowed(int bind, int other, int scan, int mods);
int kb_op_active(int op, int ctx);
int kb_ctx_for(int in_vehicle, int is_heli, int is_airplane);
int kb_conflicts(int op_a, int op_b, int key_b, int key_new);
int kb_is_default(int op, int k1, int k2, int k3);

void kb_host_init(void);                       /* every op back to defaults */
int  kb_host_get(int op, int slot);            /* slot 1..3; 0 = unbound/invalid */
const char *kb_host_op_label(int op);
const char *kb_host_category_label(int cat);   /* cat 1..6 */

typedef struct { int ok; int stole; int prev_op; int prev_slot; } KbBindResult;
/* Bind `key` to (op,slot). Any other op holding the same key in an overlapping context loses it (reported in
 * the result; WoW's "already bound to X" prompt as data). The same op's duplicate in another slot is cleared. */
KbBindResult kb_host_bind(int op, int slot, int key);
void kb_host_unbind(int op, int slot);
void kb_host_reset_op(int op);

/* Level-triggered: is `op` held right now? keys = SDL keyboard state (index = scancode), mods = held
 * modifier bits, ctx = current context. Inactive ops (wrong context) are never down. */
int kb_host_down(const unsigned char *keys, int mods, int ctx, int op);
/* Edge: does a keydown event (scan, mods) trigger `op`? */
int kb_host_event(int scan, int mods, int ctx, int op);

/* Persistence: "SHANKPIT_KEYBINDS 1" then one "op k1 k2 k3" line per non-default op. */
int kb_host_serialize(char *out, int cap);     /* bytes written (excl. NUL), -1 if cap too small */
int kb_host_deserialize(const char *text);     /* ops loaded, -1 on bad header; bad lines are skipped */

#endif
