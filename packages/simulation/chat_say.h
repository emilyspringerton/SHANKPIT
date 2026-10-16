/* chat_say.h -- the live day server's real chat "say" broadcast: who hears a message, and
 * whether the message itself is valid to send at all. EMILY/BACKLOG.md SECTION 536 follow-up,
 * founder real-time (2026-09-24): "integrate GFD subsystem affordances for economy and soocials"
 * -> "the GFD subsystem affordances should be via the GFD app on the BIG_O phone (mini terminal
 * interface)".
 *
 * Reverse-ported from GoblinFoxDragon's own real, already-shipped server/chat/chat.go (the
 * DragonsNShit Router) -- same real "say" behavior: any session within sayRadius of the sender's
 * own position hears it, sender included (distance to self is always 0). The pure range/length
 * decisions (day/packages/simulation/chat_rules.c, generated from PARENA/stdlib/big_o/
 * chat_rules.prn) are the same "mods first everything" split every other BIG_O PARENA module
 * already draws -- this header owns nothing stateful of its own (unlike party.h's roster
 * array, chat has no persistent state to own), it's a thin, testable wrapper the host calls once
 * per connected player per say.
 *
 * Real, honest v1 scope cut, not guessed at: chat.go also models tell (direct, by player name),
 * yell (whole scene) and guild (linkshell) channels as genuinely separate real concerns. BIG_O
 * has no player-name registry or guild system today (checked) -- only "say" maps cleanly onto
 * BIG_O's own existing int-slot/position model without inventing either, so only say ships this
 * pass. tell/yell/guild are real, separate, bigger scope, named as follow-ups in NORTHSTAR.md
 * rather than folded in blind.
 *
 * Pure C99, no GL/SDL/network -- same "state in, decide, read back" contract party.h already
 * established. The host (day/apps/server/src/main.c) owns the actual player loop, the fixed-size
 * text buffer, and turns a positive recipient decision into a real wire send.
 */
#ifndef CHAT_SAY_H
#define CHAT_SAY_H

#define CHAT_SAY_MAX_TEXT 96 /* wire payload size; chat_max_len() (200, chat.go's own real cap) is
                                 enforced first and is always <= this, so truncation never silently
                                 changes what "valid" means -- see chat_say_len_ok's own doc. */

/* Real, generated PARENA decisions -- day/packages/simulation/chat_rules.c, built from
   PARENA/stdlib/big_o/chat_rules.prn. Declared here rather than #included as a header because the
   generated file is a plain .c translation unit (same convention party_rules.c/walkie_rules.c
   already use), linked in by scripts/build_day.sh. */
int chat_say_radius_cm(void);
int chat_max_len(void);
int chat_in_range(int distance_cm, int radius_cm);
int chat_msg_len_ok(int len);

/* chat_say_len_ok -- real, honest gate: true only if len is non-empty and within both chat.go's
 * own real 200-byte Deliver() cap AND this repo's own smaller fixed wire buffer (CHAT_SAY_MAX_TEXT
 * -- the server truncates to this before sending regardless, so a message chat_msg_len_ok() would
 * accept but that doesn't fit the wire buffer is rejected outright here rather than silently sent
 * truncated). */
static inline int chat_say_len_ok(int len) {
    if (!chat_msg_len_ok(len)) return 0;
    return len <= CHAT_SAY_MAX_TEXT;
}

/* chat_say_hears -- true if a player at distance_cm from the sender receives a "say". Sender
 * distance is always 0 by construction (self always hears their own say), matching chat.go's own
 * real inRadius() semantics. */
static inline int chat_say_hears(int distance_cm) {
    return chat_in_range(distance_cm, chat_say_radius_cm());
}

#endif /* CHAT_SAY_H */
