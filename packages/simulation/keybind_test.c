/* keybind_test.c -- host half of the key binding system vs. the REAL generated PARENA rules.
 *   make test-keybind */
#include <stdio.h>
#include <string.h>
#include "keybind_host.h"

static int checks = 0, failures = 0;
static void ok(int c, const char *w) { checks++; if (!c) { failures++; printf("FAIL: %s\n", w); } }

enum { SC_A = 4, SC_D = 7, SC_E = 8, SC_F = 9, SC_G = 10, SC_J = 13, SC_L = 15, SC_M = 16, SC_Q = 20, SC_R = 21,
       SC_S = 22, SC_V = 25, SC_W = 26, SC_1 = 30, SC_9 = 38, SC_0 = 39, SC_ESC = 41, SC_SPACE = 44,
       SC_LCTRL = 224, SC_LSHIFT = 225, SC_LALT = 226, SC_RCTRL = 228, SC_RSHIFT = 229, SC_RALT = 230, SC_X = 27, SC_B = 5 };

static unsigned char keys[KB_NUM_SCANCODES];
static void press(int sc) { keys[sc] = 1; }
static void clear(void) { memset(keys, 0, sizeof keys); }
static int down(int mods, int ctx, int op) { return kb_host_down(keys, mods, ctx, op); }

static void test_defaults(void) {
    int op, ok_all = 1;
    kb_host_init();
    ok(kb_op_count() == 32, "32 ops");
    /* the old hard-coded scancodes, per context */
    clear(); press(SC_W);  ok(down(0, KB_CTX_FOOT, KB_FWD), "W forward");
    clear(); press(SC_S);  ok(down(0, KB_CTX_FOOT, KB_BACK), "S back");
    clear(); press(SC_A);  ok(down(0, KB_CTX_FOOT, KB_LEFT), "A left");
    clear(); press(SC_D);  ok(down(0, KB_CTX_FOOT, KB_RIGHT), "D right");
    clear(); press(SC_F);  ok(down(0, KB_CTX_FOOT, KB_INTERACT) && down(0, KB_CTX_PLANE, KB_INTERACT) && down(0, KB_CTX_VEHICLE, KB_INTERACT), "F interact everywhere");
    clear(); press(SC_R);  ok(down(0, KB_CTX_FOOT, KB_RELOAD) && !down(0, KB_CTX_HELI, KB_RELOAD), "R reload, not in heli");
    clear(); press(SC_E);  ok(down(0, KB_CTX_FOOT, KB_ABILITY) && !down(0, KB_CTX_HELI, KB_ABILITY) && down(0, KB_CTX_HELI, KB_HELI_RIGHT), "E ability / heli right");
    clear(); press(SC_L);  ok(down(0, KB_CTX_HELI, KB_HELI_RIGHT) && !down(0, KB_CTX_FOOT, KB_ABILITY), "L heli right only");
    clear(); press(SC_Q);  ok(down(0, KB_CTX_HELI, KB_HELI_LEFT), "Q heli left");
    clear(); press(SC_J);  ok(down(0, KB_CTX_HELI, KB_HELI_LEFT), "J heli left");
    clear(); press(SC_G);  ok(down(0, KB_CTX_FOOT, KB_PHEROMONE), "G pheromone");
    clear(); press(SC_RSHIFT); ok(down(1, KB_CTX_FOOT, KB_BOOST) && down(1, KB_CTX_FOOT, KB_AB_SHIFT), "RShift boost + page");
    clear(); press(SC_LSHIFT); ok(down(1, KB_CTX_FOOT, KB_BOOST) && down(1, KB_CTX_FOOT, KB_AB_SHIFT), "LShift boost + page (shareable)");
    clear(); press(SC_LCTRL); ok(down(2, KB_CTX_FOOT, KB_CROUCH) && down(2, KB_CTX_FOOT, KB_AB_CTRL), "LCtrl crouch + page");
    clear(); press(SC_RALT);  ok(down(4, KB_CTX_FOOT, KB_AB_ALT), "RAlt page");
    clear(); press(SC_V); ok(kb_host_event(SC_V, 0, KB_CTX_FOOT, KB_CAMERA_TOGGLE), "V camera event");
    ok(kb_host_event(SC_M, 0, KB_CTX_FOOT, KB_OVERLAY_MODE), "M overlay event");
    {   int n; for (n = 1; n <= 9; n++) { clear(); press(SC_1 + n - 1); if (!down(0, KB_CTX_FOOT, KB_AB(n))) ok_all = 0; }
        clear(); press(SC_0); if (!down(0, KB_CTX_FOOT, KB_AB(10))) ok_all = 0;
        ok(ok_all, "digits 1..9,0 are action buttons 1..10"); }
    /* defaults have no conflicts: every default key unique among ops with overlapping contexts, unless shareable */
    ok_all = 1;
    for (op = 1; op <= 32; op++) { int s; for (s = 1; s <= 3; s++) { int o2; int k = kb_default(op, s);
        if (!k) continue;
        for (o2 = 1; o2 <= 32; o2++) { int s2; for (s2 = 1; s2 <= 3; s2++)
            if (kb_conflicts(op, o2, kb_default(o2, s2), k)) ok_all = 0; } } }
    ok(ok_all, "default table has no conflicts");
    /* nothing is bound to plain Esc */
    ok_all = 1; for (op = 1; op <= 32; op++) { int s; for (s = 1; s <= 3; s++) if (kb_default(op, s) == SC_ESC) ok_all = 0; }
    ok(ok_all, "no default on Esc");
}

static void test_three_slots(void) {
    KbBindResult r;
    kb_host_init();
    ok(kb_host_get(KB_INTERACT, 1) == SC_F, "interact slot1 = F");
    ok(kb_host_get(KB_INTERACT, 2) == 0 && kb_host_get(KB_INTERACT, 3) == 0, "interact slots 2,3 free");
    r = kb_host_bind(KB_INTERACT, 2, kb_key_make(SC_X, 0));
    ok(r.ok && r.stole && r.prev_op == KB_FLAPS, "bind slot 2 steals X from flaps (interact spans the plane ctx)");
    ok(kb_host_get(KB_FLAPS, 1) == 0, "X stolen from flaps");
    r = kb_host_bind(KB_INTERACT, 3, kb_key_make(SC_B, 2));
    ok(r.ok && kb_host_get(KB_INTERACT, 3) == kb_key_make(SC_B, 2), "bind slot 3 with ctrl modifier");
    clear(); press(SC_F);  ok(down(0, KB_CTX_FOOT, KB_INTERACT), "slot1 still works");
    clear(); press(SC_X);  ok(down(0, KB_CTX_FOOT, KB_INTERACT), "slot2 works");
    clear(); press(SC_B);  ok(!down(0, KB_CTX_FOOT, KB_INTERACT), "slot3 needs Ctrl");
    clear(); press(SC_B);  ok(down(2, KB_CTX_FOOT, KB_INTERACT), "slot3 with Ctrl held");
    r = kb_host_bind(KB_INTERACT, 4, 1); ok(!r.ok, "slot 4 rejected");
    r = kb_host_bind(KB_INTERACT, 0, 1); ok(!r.ok, "slot 0 rejected");
    r = kb_host_bind(99, 1, 1); ok(!r.ok, "bad op rejected");
    r = kb_host_bind(KB_INTERACT, 1, SC_ESC); ok(!r.ok && kb_host_get(KB_INTERACT, 1) == SC_F, "plain Esc not bindable");
    r = kb_host_bind(KB_INTERACT, 1, kb_key_make(SC_ESC, 1)); ok(r.ok, "Shift+Esc is bindable");
    kb_host_unbind(KB_INTERACT, 1); ok(kb_host_get(KB_INTERACT, 1) == 0, "unbind");
    clear(); press(SC_F); ok(!down(0, KB_CTX_FOOT, KB_INTERACT), "unbound F no longer interacts");
}

static void test_conflict_steal(void) {
    KbBindResult r;
    kb_host_init();
    r = kb_host_bind(KB_RELOAD, 2, kb_key_make(SC_F, 0));    /* F is interact's */
    ok(r.ok && r.stole && r.prev_op == KB_INTERACT && r.prev_slot == 1, "stealing F reports previous owner");
    ok(kb_host_get(KB_INTERACT, 1) == 0, "interact lost F");
    clear(); press(SC_F);
    ok(down(0, KB_CTX_FOOT, KB_RELOAD) && !down(0, KB_CTX_FOOT, KB_INTERACT), "F now reloads");
    /* same key twice on one op: moves, not duplicates */
    r = kb_host_bind(KB_RELOAD, 3, kb_key_make(SC_F, 0));
    ok(r.ok && !r.stole && kb_host_get(KB_RELOAD, 2) == 0 && kb_host_get(KB_RELOAD, 3) == kb_key_make(SC_F, 0), "own duplicate collapses");
    /* shareable: binding Shift to an unrelated op does not steal the page modifier */
    kb_host_init();
    r = kb_host_bind(KB_CROUCH, 2, kb_key_make(SC_LSHIFT, 0));
    ok(r.ok && r.stole && r.prev_op == KB_BOOST, "crouch taking LShift steals it from boost (non-shareable)");
    ok(kb_host_get(KB_AB_SHIFT, 1) == SC_LSHIFT, "but never from the shareable page modifier");
}

static void test_context_separation(void) {
    KbBindResult r;
    kb_host_init();
    clear(); press(SC_SPACE);
    ok(down(0, KB_CTX_FOOT, KB_JUMP) && !down(0, KB_CTX_FOOT, KB_THROTTLE_UP), "Space jumps on foot");
    ok(!down(0, KB_CTX_PLANE, KB_JUMP) && down(0, KB_CTX_PLANE, KB_THROTTLE_UP), "Space is throttle in a plane");
    ok(kb_ctx_for(0, 0, 0) == KB_CTX_FOOT && kb_ctx_for(1, 0, 0) == KB_CTX_VEHICLE &&
       kb_ctx_for(1, 1, 0) == KB_CTX_HELI && kb_ctx_for(1, 0, 1) == KB_CTX_PLANE, "ctx_for");
    /* binding a plane-only op to a foot-only key steals nothing across contexts */
    r = kb_host_bind(KB_FLAPS, 2, kb_key_make(SC_G, 0));       /* G = pheromone, ctx 15 overlaps plane */
    ok(r.ok && r.stole && r.prev_op == KB_PHEROMONE, "overlapping ctx steals");
    r = kb_host_bind(KB_HELI_LEFT, 3, kb_key_make(SC_R, 0));   /* R = reload ctx 7, heli ctx 8 -> disjoint */
    ok(r.ok && !r.stole && kb_host_get(KB_RELOAD, 1) == SC_R, "disjoint ctx coexists on one key");
    clear(); press(SC_R);
    ok(down(0, KB_CTX_FOOT, KB_RELOAD) && !down(0, KB_CTX_FOOT, KB_HELI_LEFT) && down(0, KB_CTX_HELI, KB_HELI_LEFT), "R by context");
}

static void test_modifiers(void) {
    int k = kb_key_make(SC_1, 1 | 4);
    ok(kb_key_scan(k) == SC_1 && kb_key_mods(k) == 5, "encode/decode shift+alt+1");
    ok(kb_key_make(0, 1) == 0 && kb_key_make(2000, 0) == 0, "invalid scancode -> 0");
    ok(kb_match(k, SC_1, 5) && kb_match(k, SC_1, 7) && !kb_match(k, SC_1, 1) && !kb_match(k, SC_1, 0) && !kb_match(k, SC_1 + 1, 5), "match needs the demanded mods");
    ok(kb_match(kb_key_make(SC_F, 0), SC_F, 7), "plain bind matches with mods held");
    ok(kb_key_valid(0) && kb_key_valid(k) && !kb_key_valid(-1) && !kb_key_valid(1024) && !kb_key_valid(9000), "valid keys");
    /* specificity: Shift+F on one op shadows plain F while Shift is down */
    kb_host_init();
    kb_host_bind(KB_RELOAD, 2, kb_key_make(SC_F, 1));
    clear(); press(SC_F);
    ok(down(0, KB_CTX_FOOT, KB_INTERACT) && !down(0, KB_CTX_FOOT, KB_RELOAD), "F alone interacts");
    ok(!down(1, KB_CTX_FOOT, KB_INTERACT) && down(1, KB_CTX_FOOT, KB_RELOAD), "Shift+F reloads, plain F shadowed");
    ok(kb_host_event(SC_F, 1, KB_CTX_FOOT, KB_RELOAD) && !kb_host_event(SC_F, 1, KB_CTX_FOOT, KB_INTERACT), "event path shadows too");
}

static void test_serialize(void) {
    static char buf[4096], buf2[4096];
    int n, n2, i;
    kb_host_init();
    n = kb_host_serialize(buf, sizeof buf);
    ok(n == (int)strlen("SHANKPIT_KEYBINDS 1\n") && strcmp(buf, "SHANKPIT_KEYBINDS 1\n") == 0, "defaults serialise to header only");
    kb_host_bind(KB_INTERACT, 2, kb_key_make(SC_X, 0));
    kb_host_bind(KB_AB(3), 1, kb_key_make(SC_G, 1));
    kb_host_unbind(KB_FWD, 1);
    n = kb_host_serialize(buf, sizeof buf);
    ok(n > 20, "non-default serialises");
    kb_host_init();
    ok(kb_host_deserialize(buf) >= 3, "deserialise loads ops");
    n2 = kb_host_serialize(buf2, sizeof buf2);
    ok(n == n2 && strcmp(buf, buf2) == 0, "round trip is byte-identical");
    ok(kb_host_get(KB_INTERACT, 2) == kb_key_make(SC_X, 0) && kb_host_get(KB_FWD, 1) == 0, "values restored");
    ok(kb_host_deserialize("garbage") == -1, "bad header rejected");
    ok(kb_host_deserialize("SHANKPIT_KEYBINDS 1\n11 9 0 0\n99 1 2 3\n11 9 0\n12 41 0 0\nxyz\n11 9 0 0 5\n") == 1, "bad lines skipped");
    ok(kb_host_get(KB_INTERACT, 1) == SC_F, "good line applied");
    ok(kb_host_serialize(buf, 10) == -1, "tiny buffer reports failure");
    for (i = 0; i < 3; i++) (void)i;
}

static void test_reset(void) {
    kb_host_init();
    kb_host_bind(KB_JUMP, 1, kb_key_make(SC_G, 0));
    kb_host_bind(KB_JUMP, 2, kb_key_make(SC_LALT, 0));
    kb_host_reset_op(KB_JUMP);
    ok(kb_host_get(KB_JUMP, 1) == SC_SPACE && kb_host_get(KB_JUMP, 2) == 0, "reset op");
    /* reset after stealing restores the stolen op's default only when asked */
    kb_host_bind(KB_RELOAD, 1, kb_key_make(SC_F, 0));
    ok(kb_host_get(KB_INTERACT, 1) == 0, "stolen");
    kb_host_init();
    ok(kb_host_get(KB_INTERACT, 1) == SC_F && kb_host_get(KB_RELOAD, 1) == SC_R, "reset all");
}

int main(void) {
    test_defaults(); test_three_slots(); test_conflict_steal(); test_context_separation();
    test_modifiers(); test_serialize(); test_reset();
    printf("keybind_test: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
