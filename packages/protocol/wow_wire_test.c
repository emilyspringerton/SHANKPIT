/* Headless tests for the macro/mod wire: multi-target intents, cast intents, macro bytecode, invoke/ack,
 * and the mod registry. The interesting assertion is the equivalence one: a macro compiled from text,
 * serialised to bytes and decoded again must be the SAME macro -- same masks, tokens and arguments. */
#include <stdio.h>
#include <string.h>
#include "wow_wire.h"
#include "wow_mod.h"
#include "wow_server.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("wowwire: FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void test_layout(void) {
    CHECK(WOW_WIRE_INTENT_BYTES == 7 && WOW_WIRE_CAST_BYTES == 6 && WOW_WIRE_INVOKE_BYTES == 5 &&
          WOW_WIRE_ACK_BYTES == 5 && WOW_WIRE_DEF_HEADER == 5 && WOW_WIRE_CLAUSE_HEADER == 6,
          "a byte count moved: intent %d cast %d invoke %d ack %d",
          WOW_WIRE_INTENT_BYTES, WOW_WIRE_CAST_BYTES, WOW_WIRE_INVOKE_BYTES, WOW_WIRE_ACK_BYTES);
    CHECK(wow_target_slots(0) == WOW_TARGET_SLOTS && wow_target_slots(1) == WOW_TARGET_SLOTS,
          "the rule's slot count and the wire's array disagree");
    CHECK(wow_macro_max_clauses(0) == WOW_WIRE_MAX_CLAUSES, "the rule's clause cap and the wire disagree");
    CHECK(wow_cast_queue(0) == 2 && wow_cast_queue(1) == 1 && wow_cast_queue(2) == 3,
          "cast queue is not the PARENA table's");
    CHECK(wow_ack_ms(0) == 250 && wow_ack_ms(1) == 500 && wow_ack_ms(2) == 120, "ack period is not PARENA's");
    CHECK(wow_target_resend_ms(0) == 500 && wow_target_resend_ms(1) == 1000, "resend period is not PARENA's");
}

static void test_targets(void) {
    WowTargets client, server;
    uint8_t buf[16];
    uint8_t stale[WOW_WIRE_INTENT_BYTES];
    int n;
    wow_targets_begin(&client);
    wow_targets_begin(&server);

    n = wow_targets_intent(&client, 0, 7, buf, sizeof buf);
    CHECK(n == WOW_WIRE_INTENT_BYTES && buf[1] == WOW_OP_SET_TARGET && buf[4] == 0,
          "a target intent did not encode (n=%d)", n);
    CHECK(wow_targets_apply(&server, buf, n) == 2 && wow_targets_get(&server, 0) == 7,
          "the server did not take the target");

    CHECK(wow_targets_intent(&client, 0, 7, buf, sizeof buf) == 0,
          "a target that did not change was re-sent (that is the wasted air)");

    n = wow_targets_intent(&client, 0, 8, buf, sizeof buf);
    CHECK(n == WOW_WIRE_INTENT_BYTES && wow_targets_apply(&server, buf, n) == 2 &&
          wow_targets_get(&server, 0) == 8, "a retarget did not apply");

    stale[0] = (uint8_t)WOW_WIRE_VERSION; stale[1] = WOW_OP_SET_TARGET;
    stale[2] = 1; stale[3] = 0; stale[4] = 0; stale[5] = 7; stale[6] = 0;
    CHECK(wow_targets_apply(&server, stale, WOW_WIRE_INTENT_BYTES) == 0 &&
          wow_targets_get(&server, 0) == 8, "a stale target intent won");

    n = wow_targets_intent(&client, 1, 12, buf, sizeof buf);
    CHECK(n > 0 && wow_targets_apply(&server, buf, n) == 2 && wow_targets_get(&server, 1) == 12,
          "a second named target slot did not work (multiple targets)");
    CHECK(wow_targets_intent(&client, 1, -1, buf, sizeof buf) == WOW_WIRE_INTENT_BYTES &&
          wow_targets_apply(&server, buf, WOW_WIRE_INTENT_BYTES) == 2 &&
          wow_targets_get(&server, 1) == -1, "clearing a slot failed");
    CHECK(wow_targets_intent(&client, 1, -1, buf, sizeof buf) == 0, "clearing an empty slot was sent");

    CHECK(wow_targets_intent(&client, WOW_TARGET_SLOTS, 1, buf, sizeof buf) == -1, "a bad slot encoded");
    CHECK(wow_targets_intent(&client, 0, 0, buf, sizeof buf) == -1, "entity 0 was treated as a unit");
    CHECK(wow_targets_apply(&server, buf, 3) == -1, "a truncated intent applied");
    stale[0] = 99;
    CHECK(wow_targets_apply(&server, stale, WOW_WIRE_INTENT_BYTES) == -1, "a foreign version applied");
    stale[0] = (uint8_t)WOW_WIRE_VERSION; stale[1] = WOW_OP_CAST;
    CHECK(wow_targets_apply(&server, stale, WOW_WIRE_INTENT_BYTES) == -1, "a cast op applied as a target");
    stale[1] = WOW_OP_SET_TARGET; stale[2] = 0; stale[3] = 0;
    CHECK(wow_targets_apply(&server, stale, WOW_WIRE_INTENT_BYTES) == -1, "sequence 0 applied");
}

static void test_cast(void) {
    WowCastIntent in, out;
    uint8_t buf[16];
    int n;
    memset(&in, 0, sizeof in);
    in.weapon = 4;
    in.token = WOW_TOKEN_TARGET;
    in.seq = 9;
    n = wow_cast_encode(&in, buf, sizeof buf);
    CHECK(n == WOW_WIRE_CAST_BYTES, "cast encoded %d bytes", n);
    memset(&out, 0, sizeof out);
    CHECK(wow_cast_decode(buf, n, &out) == n && out.weapon == 4 && out.token == WOW_TOKEN_TARGET &&
          out.seq == 9, "cast did not round-trip");
    buf[0] = 99;
    CHECK(wow_cast_decode(buf, n, &out) == -1, "a foreign version decoded");
    buf[0] = (uint8_t)WOW_WIRE_VERSION;
    CHECK(wow_cast_decode(buf, n - 1, &out) == -1, "a short cast decoded");
    CHECK(wow_cast_encode(NULL, buf, sizeof buf) == -1, "a null cast encoded");
}

static void test_macro_bytecode(void) {
    const char *text = "cast [harm,nodead,@mouseover] magnum; [@player] knife";
    WowMacroLine line;
    WowMacroDef def;
    uint8_t out[128];
    int n, i;
    CHECK(wow_macro_parse(text, &line) == 1, "the example did not compile");
    n = wow_macro_encode_def(3, &line, out, sizeof out);
    CHECK(n == 28, "the two-clause definition is %d bytes, expected 28", n);
    CHECK(n > 0 && strlen(text) > (size_t)(6 * n / 4) - 20, "sanity: text %d bytes vs %d on the wire",
          (int)strlen(text), n);
    memset(&def, 0, sizeof def);
    CHECK(wow_macro_decode_def(out, n, &def) == n && def.id == 3 &&
          def.line.cmd == line.cmd && def.line.clause_count == line.clause_count,
          "the definition header did not survive");
    for (i = 0; i < line.clause_count; i++) {
        CHECK(def.line.clause[i].require_bits == line.clause[i].require_bits &&
              def.line.clause[i].forbid_bits == line.clause[i].forbid_bits &&
              def.line.clause[i].token == line.clause[i].token &&
              strcmp(def.line.clause[i].arg, line.clause[i].arg) == 0,
              "clause %d changed on the wire", i);
    }
    CHECK(wow_macro_decode_def(out, n + 1, &def) == -1, "trailing bytes were ignored");
    CHECK(wow_macro_encode_def(0, &line, out, sizeof out) == -1, "macro id 0 was encoded");
    CHECK(wow_macro_encode_def(3, &line, out, 8) == -1, "a too-small buffer was written into");
    out[1] = (uint8_t)WOW_OP_DEFINE_MACRO; out[2] = 3; out[3] = WOW_CMD_CAST; out[4] = 0;
    CHECK(wow_macro_decode_def(out, 5, &def) == -1, "a zero-clause definition was accepted");
    out[4] = (unsigned char)(WOW_WIRE_MAX_CLAUSES + 1);
    CHECK(wow_macro_decode_def(out, 5, &def) == -1, "an over-cap clause count was accepted");
}

static void test_invoke_ack(void) {
    WowInvoke v, v2;
    WowAck a, a2;
    uint8_t buf[16];
    v.macro_id = 9; v.seq = 300;
    CHECK(wow_invoke_encode(&v, buf, sizeof buf) == WOW_WIRE_INVOKE_BYTES &&
          wow_invoke_decode(buf, WOW_WIRE_INVOKE_BYTES, &v2) == WOW_WIRE_INVOKE_BYTES &&
          v2.macro_id == 9 && v2.seq == 300, "invoke did not round-trip");
    v.macro_id = 0;
    CHECK(wow_invoke_encode(&v, buf, sizeof buf) == -1, "macro id 0 was invoked");
    a.seq = 300; a.kind = WOW_OP_CAST;
    CHECK(wow_ack_encode(&a, buf, sizeof buf) == WOW_WIRE_ACK_BYTES &&
          wow_ack_decode(buf, WOW_WIRE_ACK_BYTES, &a2) == WOW_WIRE_ACK_BYTES &&
          a2.seq == 300 && a2.kind == WOW_OP_CAST, "ack did not round-trip");
}

static void test_mods(void) {
    WowModRegistry r;
    const WowModMacro *m;
    wow_mod_begin(&r);
    CHECK(wow_mod_register_macro(&r, "PULL", 1, "cast [harm] magnum; [@player] knife") == 1,
          "a valid mod macro was refused");
    CHECK(wow_mod_register_macro(&r, "pull", 2, "cast magnum") == -1, "a duplicate NAME was accepted");
    CHECK(wow_mod_register_macro(&r, "OTHER", 1, "cast magnum") == -1, "a duplicate ID was accepted");
    CHECK(wow_mod_register_macro(&r, "BAD", 3, "/fly") == 0, "text the compiler rejects was registered");
    CHECK(wow_mod_register_macro(&r, "", 4, "cast magnum") == 0, "an unnamed macro was registered");
    m = wow_mod_macro_by_name(&r, "pull");
    CHECK(m && m->id == 1 && m->line.clause_count == 2, "the macro was not findable by name");
    CHECK(wow_mod_macro_by_id(&r, 1) == m && wow_mod_macro_by_id(&r, 7) == NULL,
          "id lookup is wrong");
    CHECK(wow_mod_register_spell(&r, "bolt", "sniper") == 1, "a valid alias was refused");
    CHECK(wow_mod_spell_weapon(&r, "bolt") == wow_weapon_from_name("sniper"),
          "the alias did not resolve to the engine's sniper");
    CHECK(wow_mod_spell_weapon(&r, "magnum") == wow_weapon_from_name("magnum"),
          "a plain weapon name stopped working");
    CHECK(wow_mod_spell_weapon(&r, "rocket") == -1, "an unknown name resolved to a weapon");
    CHECK(wow_mod_register_spell(&r, "rocket", "nope") == 0, "an alias for a missing weapon was accepted");
    CHECK(wow_mod_register_spell(&r, "BOLT", "knife") == -1, "a duplicate alias was accepted");
}

/* ---- the server end: fail closed, then IDUNA-authenticated intents ---------------------------- */

static const unsigned char fake_ticket[WOW_WIRE_TICKET_BYTES] = "TICKET-OK";

static int fake_auth(void *ctx, const unsigned char *ticket, int len, char *pid, int cap) {
    (void)ctx;
    if (len == WOW_WIRE_TICKET_BYTES && memcmp(ticket, fake_ticket, WOW_WIRE_TICKET_BYTES) == 0) {
        if (cap > 9) memcpy(pid, "player-7", 9);
        return 1;
    }
    return 0;
}

static void truth_hostile(void *ctx, int client, uint16_t entity, WowUnit *u) {
    (void)ctx; (void)client;
    if (entity == 7) { u->hostile = 1; u->dead = 0; }
}

static void test_server(void) {
    WowServer s;
    WowTargets tx;
    WowAuthFrame af;
    uint8_t frame[128], ack[WOW_WIRE_ACK_BYTES];
    int n;
    WowCastIntent ci, taken;

    wow_server_begin(&s);
    CHECK(s.require_auth == 1, "the server started open (must fail closed)");

    /* no verifier installed: even a well-formed AUTH is refused */
    memset(&af, 0, sizeof af);
    memcpy(af.ticket, fake_ticket, WOW_WIRE_TICKET_BYTES);
    n = wow_auth_encode(&af, frame, sizeof frame);
    CHECK(n == WOW_WIRE_AUTH_BYTES, "auth frame is %d bytes", n);
    CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == -2, "auth was accepted with no verifier");

    s.auth = fake_auth;
    af.ticket[0] = 'N';
    n = wow_auth_encode(&af, frame, sizeof frame);   /* the FRAME is what the server reads */
    CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == -2, "a bad ticket was accepted");

    memset(&af, 0, sizeof af);
    memcpy(af.ticket, fake_ticket, WOW_WIRE_TICKET_BYTES);
    n = wow_auth_encode(&af, frame, sizeof frame);
    CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == 1 && s.c[1].authed == 1 &&
          strcmp(s.c[1].player_id, "player-7") == 0 && ack[1] == WOW_OP_ACK,
          "the IDUNA verifier's identity was not bound to the client");

    /* an unauthenticated other client is refused */
    {
        WowTargets t2;
        uint8_t b2[16];
        int n2;
        wow_targets_begin(&t2);
        n2 = wow_targets_intent(&t2, 0, 7, b2, sizeof b2);
        CHECK(wow_server_rx(&s, 2, b2, n2, ack, sizeof ack) == -2, "an unauthenticated target was applied");
    }

    /* targets: applied, then a duplicate is dropped */
    wow_targets_begin(&tx);
    n = wow_targets_intent(&tx, 0, 7, frame, sizeof frame);
    CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == 1 && wow_targets_get(&s.c[1].targets, 0) == 7,
          "the server did not take the target");
    CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == 0, "a duplicate target intent was acknowledged");

    /* casts: queued, taken, bounded by PARENA's queue */
    memset(&ci, 0, sizeof ci);
    ci.weapon = (uint8_t)wow_weapon_from_name("magnum");
    ci.token = WOW_TOKEN_TARGET;
    ci.seq = 5;
    n = wow_cast_encode(&ci, frame, sizeof frame);
    CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == 1, "a valid cast was refused");
    CHECK(wow_server_take_cast(&s, 1, &taken) == 1 && taken.weapon == ci.weapon && taken.seq == 5,
          "the cast did not come out of the queue");
    CHECK(wow_server_take_cast(&s, 1, &taken) == 0, "the queue did not drain");

    ci.seq = 6; wow_cast_encode(&ci, frame, sizeof frame);
    CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == 1, "cast 1 refused");
    ci.seq = 7; wow_cast_encode(&ci, frame, sizeof frame);
    CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == 1, "cast 2 refused");
    ci.seq = 8; wow_cast_encode(&ci, frame, sizeof frame);
    CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == 0,
          "a third cast was queued past PARENA's depth (2 by default)");
    ci.weapon = 99; ci.seq = 9; wow_cast_encode(&ci, frame, sizeof frame);
    CHECK(wow_server_rx(&s, 1, frame, n, ack, sizeof ack) == -1, "a cast for a weapon that does not exist was queued");

    while (wow_server_take_cast(&s, 1, &taken)) { }   /* the earlier casts must not fill this queue */

    /* macro define + invoke, evaluated against the server's own target table */
    {
        WowMacroLine line;
        WowInvoke inv;
        uint8_t def[128];
        int dn;
        CHECK(wow_macro_parse("cast [harm] magnum; [@player] knife", &line) == 1, "compile failed");
        dn = wow_macro_encode_def(4, &line, def, sizeof def);
        CHECK(dn > 0 && wow_server_rx(&s, 1, def, dn, ack, sizeof ack) == 1, "a macro definition was refused");
        s.unit_truth = truth_hostile;
        s.unit_ctx = NULL;
        inv.macro_id = 4; inv.seq = 11;
        n = wow_invoke_encode(&inv, def, sizeof def);
        CHECK(wow_server_rx(&s, 1, def, n, ack, sizeof ack) == 1, "invoking a defined macro failed");
        CHECK(wow_server_take_cast(&s, 1, &taken) == 1 &&
              taken.weapon == (uint8_t)wow_weapon_from_name("magnum") && taken.token == WOW_TOKEN_TARGET,
              "[harm] against a hostile target did not queue the magnum at the target");
        wow_targets_clear(&s.c[1].targets, 0);
        inv.seq = 12;
        n = wow_invoke_encode(&inv, def, sizeof def);
        CHECK(wow_server_rx(&s, 1, def, n, ack, sizeof ack) == 1, "the fallback clause did not run");
        CHECK(wow_server_take_cast(&s, 1, &taken) == 1 &&
              taken.weapon == (uint8_t)wow_weapon_from_name("knife") && taken.token == WOW_TOKEN_PLAYER,
              "with no target the macro did not fall through to [@player] knife");
        inv.macro_id = 9;
        n = wow_invoke_encode(&inv, def, sizeof def);
        CHECK(wow_server_rx(&s, 1, def, n, ack, sizeof ack) == 0, "an undefined macro id was served");
    }

    CHECK(wow_server_rx(&s, WOW_SERVER_CLIENTS, frame, n, ack, sizeof ack) == -1, "an out-of-range client was served");
    CHECK(wow_server_rx(NULL, 0, frame, n, ack, sizeof ack) == -1, "a null server was served");
}

int main(void) {
    test_layout();
    test_targets();
    test_cast();
    test_macro_bytecode();
    test_invoke_ack();
    test_mods();
    test_server();
    if (fails) { printf("wowwire: %d FAILED\n", fails); return 1; }
    printf("wowwire: all checks passed\n");
    return 0;
}
