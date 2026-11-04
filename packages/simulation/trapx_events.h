/* packages/simulation/trapx_events.h -- TRAPX's MODS FIRST surface: every city transition announces an event,
 * mods subscribe to a mask of event kinds, and delivery is ordered and deterministic.
 *
 * The discipline this follows is ECOWAR's Living Map (docs/NORTHSTAR_LIVING_MAP.md's "Mod event model,
 * honestly"): PARENA VS0 has no function pointers or closures, so there is no dynamic registration of *code*.
 * What IS real, and what this header is, is the other half of "mods first": a bus that every transition writes
 * into, a named mod registry with a subscription mask and a priority, and one ordered delivery pass a host runs
 * each tick. A mod is a name plus a mask plus a priority -- adding one does not touch the city, and a mod that
 * wants CLAIM and KILL cannot accidentally be handed TICK events.
 *
 * The events are the API. Anything a mod could want to know is one of these kinds; nothing reaches into the city
 * struct to poll it, which is what makes an out-of-tree mod possible at all.
 */
#ifndef SHANKPIT_TRAPX_EVENTS_H
#define SHANKPIT_TRAPX_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

#define TRAPX_EVENT_CAP 256

/* Event vocabulary. Keep this list and trapx_event_name() in step -- a test asserts every kind has a name. */
enum {
    TRAPX_EV_TICK = 0,      /* a scene simulated one step: a=alertness b=enforcement */
    TRAPX_EV_CLAIM,         /* a Field Office was claimed: a=scene b=flow spent */
    TRAPX_EV_CONTEST,       /* a claim was contested: a=scene b=pressure */
    TRAPX_EV_K9,            /* K9 units deployed: a=scene b=units */
    TRAPX_EV_ALERT,         /* watcher alertness crossed a 25-point band: a=alertness b=old alertness */
    TRAPX_EV_ENFORCE,       /* enforcement level changed: a=level b=previous level */
    TRAPX_EV_RECEIPT,       /* the ledger grew: a=kind b=amount */
    TRAPX_EV_KILL,          /* a killing in public: a=witnesses b=heat added */
    TRAPX_EV_ROGUE,         /* an office fell apart: a=integrity b=attention */
    TRAPX_EV_PORTAL,        /* the player crossed scenes: a=from b=to */
    TRAPX_EV_COUNT
};

typedef struct {
    int kind;
    int tick;
    int scene;
    int a;
    int b;
} TrapxEvent;

typedef struct {
    TrapxEvent ring[TRAPX_EVENT_CAP];
    int head;                    /* next write slot */
    int count;                   /* how many are valid, <= CAP */
    unsigned long seq;           /* monotonic, survives the ring wrapping */
} TrapxEvents;

void trapx_events_reset(TrapxEvents *e);
/* Append one event. Returns the sequence number, or 0 on bad input (a null bus or an unknown kind). */
unsigned long trapx_events_push(TrapxEvents *e, int kind, int tick, int scene, int a, int b);
int trapx_events_count(const TrapxEvents *e);
/* Newest first: index 0 is the most recent event still in the ring. NULL when out of range. */
const TrapxEvent *trapx_events_at(const TrapxEvents *e, int index);
const char *trapx_event_name(int kind);

/* ---- the mod registry ------------------------------------------------------------------------- */
#define TRAPX_MOD_CAP 8
#define TRAPX_MOD_NAMELEN 32

typedef struct {
    char name[TRAPX_MOD_NAMELEN];
    unsigned mask;               /* bit (1u << kind) */
    int priority;                /* higher runs first; ties break on registration order */
    int enabled;
} TrapxMod;

typedef struct {
    TrapxMod mods[TRAPX_MOD_CAP];
    int count;
} TrapxMods;

void trapx_mods_reset(TrapxMods *m);
/* Register a mod. Returns its index, or -1 when the registry is full or the name is empty/too long. A duplicate
 * name is refused too: two mods with one name is a bug a host must see, not a silent overwrite. */
int trapx_mods_add(TrapxMods *m, const char *name, unsigned mask, int priority);
int trapx_mods_index(const TrapxMods *m, const char *name);
int trapx_mods_enable(TrapxMods *m, int index, int on);
/* Does this mod want this kind? Disabled mods want nothing. Out-of-range index or kind answers 0. */
int trapx_mods_wants(const TrapxMods *m, int index, int kind);
/* The mods that want `ev`, highest priority first, into `out`. Returns how many were written. This is the one
 * delivery pass a host runs -- there is no callback table to keep in sync. */
int trapx_mods_deliver(const TrapxMods *m, const TrapxEvent *ev, int *out, int out_cap);

#ifdef __cplusplus
}
#endif

#endif /* SHANKPIT_TRAPX_EVENTS_H */
