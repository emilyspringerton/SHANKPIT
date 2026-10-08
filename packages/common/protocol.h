#ifndef PROTOCOL_H
#define PROTOCOL_H

#ifndef _WIN32
#include <netinet/in.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "../simulation/day_night_clock.h" /* DayNightClock -- ServerState.story_clock below,
    EMILY/BACKLOG.md SECTION 536 follow-up ("server-authoritative day/night sync") */

#define MAX_CLIENTS 70
#define MAX_WEAPONS 9
#define MAX_PROJECTILES 1024
#define MAX_HELICOPTERS 8
#define MAX_BUGGIES 16
#define MAX_HORSES  8
#define LAG_HISTORY 64

#define SCENE_GARAGE_OSAKA 0
#define SCENE_STADIUM 1
#define SCENE_VOXWORLD 2
#define SCENE_DUST_COMPOUND 3
#define SCENE_CITY 4
#define SCENE_OIL_TANKER 5
#define SCENE_POO_POO_ISLAND 6
#define SCENE_STORY_CAVE 7
#define SCENE_RACE_TRACK 8   /* Bedrock Racers — StaticBackend, no Dragonfly dependency */
#define SCENE_CUSTOM_LEVEL 9 /* a level authored in NOCK's SHANKPIT level editor (EMILY/
                                 BACKLOG.md SECTION 459) and loaded via phys_set_custom_level --
                                 see packages/world/level_boxes.h's own doc comment */

#define PACKET_CONNECT 0
#define PACKET_USERCMD 1
#define PACKET_SNAPSHOT 2
#define PACKET_WELCOME  3
#define PACKET_VOXEL_DATA    4
#define PACKET_IMPACT        5
#define PACKET_SCENE_CHANGE  6
#define PACKET_DISCONNECT    7
/* 8, 9 reserved by BEDWARS_SPEC.md (PacketBedEvent, PacketResourcePickup) — not yet implemented */
#define PACKET_RACING_STATE  10
/* PACKET_PHEROMONE_THROW -- BIG_O engine merge phase 5 (EMILY/BACKLOG.md SECTION 536), ported in
 * from BIG_O's own PC_PACKET_PHEROMONE_THROW (day/packages/common/papercraft_protocol.h): client
 * -> server, a thrown pheromone marker (docs/DESIGN_DIGEST.md §6's "pheromone balls/darts"). Same
 * "reserved, not yet implemented" precedent as 8/9 above -- see PheromoneThrowPacket below and
 * packages/common/pheromone.h's own header comment for why: no live zombie/citizen NPC entity
 * array exists yet for a marker to steer (that's phase 7). Client-side derivation matches BIG_O's
 * own v0 (sender's own position + camera-forward * a fixed throw distance), not a real projectile. */
#define PACKET_PHEROMONE_THROW 11

/* PACKET_WORLD_CLOCK -- EMILY/BACKLOG.md SECTION 536 follow-up ("server-authoritative day/night
 * sync"), server -> client, MODE_STORY/MODE_STORY_CAVE only. A real, whole-state snapshot of
 * local_state.story_clock (not a delta), broadcast alongside PACKET_SNAPSHOT so a genuine
 * networked story client renders the SAME sky/weather every other connected client sees, instead
 * of ticking its own independent, silently-drifting local clock. Every other mode never sends
 * this at all -- a real, deliberate v0 cut named in this thread's own doc comments rather than
 * broadcasting a clock nobody's gameplay depends on. See NetWorldClock below (defined after
 * NetHeader). */
#define PACKET_WORLD_CLOCK 12

/* PACKET_BRICK_STATE -- destructible brick (packages/world/brick_fracture.h; founder real-time,
 * 2026-10-01: "i want brick to be destructable"). Server -> client. Carries up to
 * NET_BRICK_MAX_ENTRIES entries of "brick cell (parent box index, cell key) now has hp H". Entries
 * are absolute and idempotent -- applying one twice, late, or out of order cannot desync anything --
 * so the server sends each change a few times and cycles the whole damaged set in the background
 * (a late joiner or a dropped datagram heals itself) with no acknowledgement traffic. The client
 * validates every field (a packet is untrusted input) and never applies damage itself on a network
 * match: it only mirrors this. Custom-level (NOCK) maps only; any other scene sends nothing. */
#define PACKET_BRICK_STATE 13

/* PACKET_QUEUE_LEVEL -- card #481 ("ensure that the queue works intelligently it needs to start
 * tracking what level the user is actually on"). Server -> client, MODE_QUEUE only, sent once a
 * second to every welcomed client. Carries the registry id of the custom level the server is
 * simulating right now (-1 = built-in SCENE_OIL_TANKER fallback). Needed because every custom level
 * shares one scene_id (SCENE_CUSTOM_LEVEL) so neither PACKET_WELCOME nor PACKET_SNAPSHOT can tell
 * the client the level changed at a round boundary; the client used to latch its first fetch for
 * the whole connection and kept stale geometry. Absolute and idempotent, like PACKET_BRICK_STATE. */
#define PACKET_QUEUE_LEVEL 14

/* PACKET_SHIELD_PACKS -- card #541 (25% shield-pack drop on death in queue). Server -> client, MODE_QUEUE only.
 * The WHOLE set of shield packs currently lying in the world (not a delta), a few times a second to every
 * welcomed client, so a dropped datagram or a late joiner heals itself. The server alone rolls the drop and
 * grants the refill (see packages/simulation/shield_packs.h); a client only renders this list. */
#define PACKET_SHIELD_PACKS 15
#define NET_SHIELD_PACK_MAX 16

#define VOXEL_CHUNK_SIZE            16
#define VOXEL_MAX_BLOCKS_PER_CHUNK  1024
#define VOXEL_BLOCK_STONE           1
#define VOXEL_BLOCK_GRASS           2
#define VOXEL_BLOCK_DIRT            3
#define VOXEL_BLOCK_LOG             17
#define VOXEL_BLOCK_LEAF            18
#define VOXEL_CHUNK_CACHE_SIZE      32

typedef struct {
    int            active;
    int            chunk_x;
    int            chunk_z;
    int            block_count;
    unsigned char  bx[VOXEL_MAX_BLOCKS_PER_CHUNK];
    unsigned char  by[VOXEL_MAX_BLOCKS_PER_CHUNK];
    unsigned char  bz[VOXEL_MAX_BLOCKS_PER_CHUNK];
    unsigned short block_id[VOXEL_MAX_BLOCKS_PER_CHUNK];
} VoxelChunkCache;

#define STATE_ALIVE 0
#define STATE_DEAD  1
/* STATE_SPECTATOR -- defined since before this comment existed but, checked directly, never
 * actually read by any collision/gravity/render code anywhere in this file's own consumers
 * (packages/common/physics.h, packages/simulation/local_game.h) -- a real, honest gap, not a
 * working feature nobody happened to use. Made real for TYLER VALHANNA's cold open (S536, "you
 * are a floating orb like a wisp... no collision... flies through walls"): phys_tick_player
 * (physics.h) now skips AABB collision and gravity entirely when p->state==STATE_SPECTATOR,
 * and local_update/server_tick (local_game.h / apps/server/src/main.c) drive full 3D free-fly
 * movement (pitch-aware forward vector, not the ground-locked yaw-only vector every other state
 * uses) instead of the usual accelerate()+gravity path. This is a genuinely reusable engine
 * capability now, not a TYLER-only hack -- any future spectator/observer/ghost-camera need in
 * any mode gets it for free. */
#define STATE_SPECTATOR 2

#define WPN_KNIFE 0
#define WPN_MAGNUM 1
#define WPN_AR 2
#define WPN_SHOTGUN 3
#define WPN_SNIPER 4
#define WPN_KATANA 5
#define WPN_MISSILE 6
#define WPN_FLASHLIGHT 7 /* founder real-time: "can we add a weapon 7 ... a flashlight - use a
                             cone with a shader - at night in shankpit it gets dark." A pure
                             utility slot, not a combat weapon (0 damage, no ammo) -- see
                             update_weapons()'s own early-return for WPN_FLASHLIGHT in physics.h.
                             The beam itself (a real GLSL-shaded cone) and the local world-
                             brightening it does are both client-side-only, see apps/lobby's own
                             draw_flashlight_beam/flashlight_box_boost. */

#define WPN_HAMMER 8 /* card #447: a heavy melee tool. Slow swing, big player damage, and it is the one weapon
                         made for DEMOLITION: a swing also strikes the wall in front of you (reach
                         HAMMER_REACH in physics.h) through the map-damage surface hook, with PARENA's
                         brick_rules.prn multiplying its damage against masonry. No ammo, no reload. */

#define RELOAD_TIME_FULL 60
#define RELOAD_TIME_TACTICAL 42
#define SHIELD_REGEN_DELAY 180

/* Missile launcher (WPN_MISSILE) is SHANKPIT's first travelling-projectile
 * base weapon -- every other weapon is a hitscan raycast (see
 * update_weapons() in physics.h). It reuses the same Projectile/
 * spawn_projectile/update_projectiles pipeline the sniper's storm-charge
 * ultimate already exercises, plus a real splash-damage detonation
 * (explode_splash() in local_game.h) gated by Projectile.splash_radius so
 * the storm-charge shot (splash_radius left at 0) is untouched. */
#define MISSILE_SPLASH_RADIUS 6.0f

typedef struct {
    int active;
    unsigned int last_heard_ms;
} ClientMeta;

typedef struct {
    unsigned char type;
    unsigned char client_id;
    unsigned short sequence;
    unsigned int timestamp;
    unsigned char entity_count; 
    unsigned char scene_id;
} NetHeader;

/* NetWorldClock -- see PACKET_WORLD_CLOCK's own doc comment above. A real, protocol-owned mirror
 * of DayNightClock's own broadcastable fields (everything except `rng`, which is a server-
 * internal weather-scheduling detail a client never needs) -- deliberately a separate type from
 * DayNightClock itself so the wire format doesn't leak the internal simulation struct across the
 * network boundary, same judgment this file's other Net* structs already make. */
typedef struct {
    NetHeader hdr;
    int minutes;
    int start_minute;
    unsigned char weather;
    int weather_ends;
} NetWorldClock;

/* NetQueueLevel -- see PACKET_QUEUE_LEVEL's own doc comment above. */
typedef struct {
    NetHeader hdr;
    int level_id;
} NetQueueLevel;

/* NetShieldPacks -- see PACKET_SHIELD_PACKS's own doc comment above. Send only the first `count` entries:
 * len = offsetof(NetShieldPacks, p) + count * sizeof(NetShieldPack). */
typedef struct { float x, y, z; } NetShieldPack;
typedef struct {
    NetHeader hdr;
    unsigned char count;
    unsigned char pad[3];
    NetShieldPack p[NET_SHIELD_PACK_MAX];
} NetShieldPacks;

/* NetBrickState -- see PACKET_BRICK_STATE's own doc comment above. key_lo/key_hi are the two halves
 * of the 30-bit cell key (ix | iy<<10 | iz<<20) so the entry has no padding or endianness trap. */
#define NET_BRICK_MAX_ENTRIES 40
typedef struct {
    unsigned short parent;
    unsigned short key_lo;
    unsigned short key_hi;
    unsigned char hp;
    unsigned char pad;
} NetBrickEntry;

typedef struct {
    NetHeader hdr;
    unsigned char count;
    unsigned char pad[3];
    NetBrickEntry e[NET_BRICK_MAX_ENTRIES];
} NetBrickState;

typedef struct {
    unsigned int sequence;
    unsigned int timestamp;
    unsigned short msec;
    float fwd; float str;
    float yaw; float pitch;    
    unsigned int buttons;
    int weapon_idx;
} UserCmd;

#define BTN_JUMP   1
#define BTN_ATTACK 2
#define BTN_CROUCH 4
#define BTN_RELOAD 8
#define BTN_USE    16
#define BTN_ABILITY_1 32
#define BTN_VEHICLE_2 64
#define BTN_ULTIMATE 128   /* Bedrock Racers — spend ultimate charge (racing scene only) */
#define BTN_PHEROMONE 256  /* MODE_ZOMBIES/MODE_SURVIVAL pheromone marker throw (G key, apps/lobby),
    BIG_O/NORTHSTAR.md section 10's own command tool. Deliberately its own bit, not reused off
    BTN_USE -- BTN_USE already carries generic door/button/CTF-flag interact semantics
    (story_buttons.h), and this is a dedicated, always-available tool, not a context interact. */

#define VEH_NONE  0
#define VEH_BUGGY 1
#define VEH_BIKE  2
#define VEH_HELICOPTER 3
#define VEH_HORSE 4   /* Icelandic tölt mount — Toledo 1040 CE */

/* Bedrock Racers item slots — held one at a time, consumed via BTN_ABILITY_1 */
#define RACE_ITEM_NONE  0
#define RACE_ITEM_BOOST 1
#define RACE_ITEM_SHIELD 2
#define RACE_ITEM_TRAP  3

/* Documents the PACKET_RACING_STATE per-racer wire layout (8 bytes, packed,
 * no padding): client_id(1) lap(1) checkpoint_idx(1) item_slot(1)
 * ultimate_charge(1) speed(4, LE float). This struct would NOT match that
 * layout if cast directly onto the buffer — the compiler pads after the 5th
 * char to align `speed`, growing it to 12 bytes. Parse the wire format by
 * hand (see net_process_racing_state() in apps2/lobby/src/main.c), not via
 * a cast to this type. */
typedef struct {
    unsigned char client_id;
    unsigned char lap;
    unsigned char checkpoint_idx;
    unsigned char item_slot;
    unsigned char ultimate_charge; /* 0-100 */
    float speed;
} RacingTelemetry;

typedef struct {
    int id;
    int dmg; int rof; int cnt; float spr; int ammo_max;
} WeaponStats;

static const WeaponStats WPN_STATS[MAX_WEAPONS] = {
    {WPN_KNIFE,   200, 20, 1, 0.0f,  0},
    {WPN_MAGNUM,  45, 25, 1, 0.0f,  8},
    {WPN_AR,      20, 6,  1, 0.04f, 30},
    {WPN_SHOTGUN, 128, 17, 8, 0.15f, 8},
    {WPN_SNIPER,  101, 52, 1, 0.0f,  5},
    {WPN_KATANA,   40, 28, 1, 0.0f,  0},
    {WPN_MISSILE, 130, 95, 1, 0.0f,  3},
    {WPN_FLASHLIGHT, 0, 1, 0, 0.0f,  0},
    {WPN_HAMMER,  70, 42, 1, 0.0f,  0}
};

typedef struct {
    int active; float x, y, z; float vx, vy, vz; int owner_id;
    int bounces_left;
    int damage;
    unsigned char scene_id;
    float splash_radius; /* 0 = point damage (existing behavior); >0 = AOE detonation, see explode_splash() */
} Projectile;

typedef struct {
    unsigned char id; 
    unsigned char scene_id;
    unsigned char is_bot;
    signed char team_id;
    unsigned int last_seq;
    float x, y, z; float yaw, pitch;
    unsigned char current_weapon;
    unsigned char state;
    unsigned char health;
    unsigned char shield;
    unsigned char is_shooting;
    unsigned char crouching;
    float reward_feedback; 
    unsigned char ammo;
    unsigned char in_vehicle;
    signed char carried_flag_team_id;
    unsigned char hit_feedback; 
    unsigned char storm_charges;
    unsigned short kills;
    unsigned short deaths;
    unsigned short death_elapsed_ms;
    unsigned short death_duration_ms;
    float death_dir_x;
    float death_dir_z;
    // reload_timer / ability_cooldown -- S459-47, founder real-time (SHANKPIT bot-league
    // observation feedback): "also gun reload state" + "make sure the features understand that
    // there are 2 different cooldowns" (WPN_SNIPER's storm-charge activation and WPN_KATANA's
    // dash both gate on the SAME server-side PlayerState.ability_cooldown timer -- see
    // packages/common/physics.h's katana_try_start_dash/update_weapons: activating sniper storm
    // sets ability_cooldown=480 AND grants storm_charges=5; the katana dash can't fire again
    // until that same shared cooldown reaches 0, even though storm_charges (unspent sniper
    // "ultimate ammo") persists independently and stays available whenever the player switches
    // back to the sniper -- these are two real, distinct, independently-tracked resources, not
    // one). Neither field existed on the wire before this commit -- a real, honest gap named in
    // docs/BOT_TRAINING_NORTHSTAR.md's first draft, closed here. Raw server ticks remaining (0 =
    // ready), not pre-normalized, so a consumer can scale against whichever real constant is
    // relevant (RELOAD_TIME_FULL/RELOAD_TIME_TACTICAL for reload_timer; the real, weapon-specific
    // ability cooldown constants in physics.h -- 260/340/420/480 -- for ability_cooldown).
    unsigned short reload_timer;
    unsigned short ability_cooldown;
    // kill_streak -- S459-52, real multikill mechanic (see PlayerState.kill_streak's own doc
    // comment in this same file for the full design). The real, persistent, rolling-window kill
    // count -- a consumer (the reward function, a future client-side "DOUBLE KILL" HUD callout)
    // detects a NEW multikill by combining this with the existing `kills` delta: kills increased
    // this tick AND kill_streak >= 2 means a real double-kill-or-higher just landed, with
    // kill_streak itself telling you which tier.
    unsigned char kill_streak;
    // vx/vy/vz -- S459-69, real fix for jump "rubberbanding" (S459-65/67/68's own real, named
    // root cause and the lesson from S459-68's failed attempt at reconstructing it without wire
    // support: velocity cannot be safely estimated from two position samples across an arbitrary
    // network interval when acceleration isn't constant in between -- jumping is exactly that
    // case, gravity + the instant JUMP_FORCE impulse mean vy swings hard and fast every tick).
    // The real, correct fix: put the server's own EXACT velocity on the wire so
    // client_reconcile_local_player can set it directly, not guess it. Real, honest cost: 12
    // bytes per player per snapshot -- negligible against this protocol's own real measured sizes
    // (a typical few-hundred-byte snapshot, nowhere near MTU, see S459-65/66's own investigation).
    float vx, vy, vz;
    // anim_override -- S470, real wire counterpart to PlayerState's own field of the same name
    // (see its doc comment for the full 0/1/2 GREET/DANCE convention). Only ever non-zero for a
    // story_ai-controlled bot mid-greet; a real, honest 1 byte/player/snapshot cost, same
    // "negligible against this protocol's own real measured sizes" reasoning vx/vy/vz's own
    // comment above already gives.
    unsigned char anim_override;
    // forced_kit -- wire counterpart to PlayerState's own field of the same name (S492). Same
    // real 1 byte/player/snapshot cost as anim_override above; 0 = auto, 1..5 = an explicit kit.
    unsigned char forced_kit;
    // third_person -- wire counterpart to PlayerState's own field of the same name (S536, engine
    // merge continuation). Same real 1 byte/player/snapshot cost as forced_kit above. Deliberately
    // a SEPARATE flag from forced_kit, not folded into it -- MODE_TYLER's own Duck phase was the
    // first real caller and originally gated the camera directly on "forced_kit==AI_KIT_LEELA",
    // but camera perspective and character skin are two independent concerns (a future mode could
    // want third-person on the player's own default skin, or a forced skin viewed in first person)
    // -- see draw_scene's own camera-selection block in apps/lobby/src/main.c for the real,
    // generalized consumer.
    unsigned char third_person;
} NetPlayer;

typedef struct {
    unsigned char id;
    unsigned char scene_id;
    unsigned char active;
    unsigned char grounded;
    float x, y, z;
    float vx, vy, vz;
    float yaw;
    float pitch_visual;
    float roll_visual;
    float rotor_angle;
    float rotor_speed;
    unsigned char health;
    signed char occupant_player_id;
} NetHelicopter;

typedef struct {
    unsigned char id;
    unsigned char scene_id;
    unsigned char active;
    unsigned char grounded;
    float x, y, z;
    float vx, vy, vz;
    float yaw;
    float pitch;
    float roll;
    float steer;
    signed char occupant_player_id;
} NetBuggy;

typedef struct {
    int version;
    float w_aggro;
    float w_strafe; float w_jump; float w_slide; float w_turret; float w_repel;
    float w_retreat; /* health-aware retreat multiplier (activated when hp < 30%) */
} BotGenome;

typedef struct {
    int id;
    int scene_id;
    int active; int is_bot;
    int team_id;
    float x, y, z; float vx, vy, vz; float yaw, pitch; int on_ground;
    // ground_friction -- S478b, founder real-time: "make the material friction stuff working per
    // cube" -- resolved by resolve_collision (physics.h) every tick a landing branch sets
    // on_ground, from whichever box's own material the player is actually standing on (falls
    // back to physics.h's own global FRICTION constant for the flat-floor/terrain case, which
    // has no material). apply_friction reads this instead of the flat global constant. Not
    // networked (NetPlayer, protocol.h's own wire struct) -- a real, server/lobby-only physics
    // input, same class as e.g. dash_vx/dash_vy/dash_vz above, never something a client needs to
    // see.
    float ground_friction;
    float in_fwd;
    float in_strafe;
    int in_jump; int in_shoot; int in_reload; int crouching; int in_use; int in_bike;
    int in_pheromone; /* BTN_PHEROMONE, see its own doc comment above -- MODE_ZOMBIES/MODE_SURVIVAL
        only; witness_ai_try_throw_pheromone reads this and self-rate-limits, so holding the key is
        safe and simply re-throws on its own cooldown (same "no edge-trigger needed" reasoning
        in_use's own story-button consumer needs a rising edge for, this doesn't). */
    int use_was_down; int bike_was_down;
    int in_ability;
    int current_weapon; int ammo[MAX_WEAPONS];
    int reload_timer; int attack_cooldown;
    int is_shooting; int jump_timer;
    int health; int shield; int shield_regen_timer; int state;
    int kills; int deaths; int hit_feedback; float recoil_anim;
    // kill_streak / last_kill_time_ms -- S459-52, founder real-time: "add double kills to
    // SHANKPIT and then make the bot double kill aware in terms of rewards (spike)" / "same for
    // tripple kill" / "same for killtacular (4)" / "im aware that in 4 player pvp killtacular is
    // impossible thats fine implement it still 4 player is just what we are doing right now."
    // Real, standard multikill mechanic (Halo's own real naming/window convention: consecutive
    // kills land within MULTIKILL_WINDOW_MS of each other, tracked PER ATTACKER regardless of
    // that attacker's own deaths in between -- a real kill-RATE streak, not a life streak).
    // kill_streak is the real, persistent count of kills landed within the current rolling
    // window; last_kill_time_ms is when the most recent one landed. Both live on PlayerState
    // (server-authoritative, like every other combat stat here) and reach the wire via
    // NetPlayer.kill_streak (protocol.h) -- see phys_enter_death_state's own doc comment in
    // physics.h for exactly where this updates.
    int kill_streak;
    unsigned int last_kill_time_ms;
    int level;
    int xp;
    int xp_to_next;
    unsigned int last_xp_award_time;
    int in_vehicle;
    int vehicle_type;
    int bike_gear;
    int vehicle_cooldown;
    unsigned int portal_cooldown_until_ms;
    float accumulated_reward; 
    BotGenome brain;
    unsigned int last_hit_time;
    unsigned int respawn_time;
    unsigned int death_time_ms;
    unsigned int death_duration_ms;
    float death_dir_x;
    float death_dir_z;
    int storm_charges;
    int ability_cooldown;
    int katana_slash_timer;
    int dash_timer;
    float dash_vx;
    float dash_vy;
    float dash_vz;
    int dash_hit_count;
    int dash_hit_targets[8];
    unsigned int stunned_until_ms;
    unsigned int stun_immune_until_ms;
    float run_phase;
    float run_weight;
    int carried_flag_team_id;
    unsigned int ctf_last_flag_event_ms;
    unsigned int ctf_melee_cooldown_ms;
    int ctf_bot_intent;
    float ctf_cumulative_reward;
    float ctf_last_reward;
    unsigned int ctf_last_stuck_ms;
    float ctf_last_objective_progress;
    /* Bedrock Racers — only meaningful while scene_id == SCENE_RACE_TRACK,
     * populated from PACKET_RACING_STATE (net_process_racing_state()). */
    int race_lap;
    int race_checkpoint_idx;
    int race_item_slot;
    int race_ultimate_charge;
    /* anim_override -- S470, founder real-time: "have them wave to the player when the player
       gets close and then dance before resuming patrol." story_ai.c's own ai_run_greet is the
       only writer (via ai_reset_input's own per-tick reset to 0, same convention as every other
       per-tick AI input field on this struct); the client-side render path
       (draw_player_skin_mannequin -> gband_skel_npc_draw) is the only reader. 0 = auto (the
       existing movement-driven idle/walk pick), 1 = GREET, 2 = DANCE -- kept in sync BY HAND
       with GOLDENBAND's own GBAND_SKEL_NPC_ANIM_* constants (gband_skel_npc.h), same class of
       manual cross-module sync gband_skel_npc.c's own GBAND_SKEL_NPC_MOVE_EPSILON comment
       already documents (no shared header between story_ai.c and gband_skel_npc.h by design). */
    int anim_override;
    /* forced_kit -- S492's own real, named "deeper gap" closed: a NOCK-authored LevelCharacter's
       chosen robot model, set once at story_ai_spawn_enemy() and never touched again (unlike
       anim_override above, which is a real per-tick value) -- distinct from AIRole (behavior).
       0 = auto (the existing connection-order round-robin / witness_ai role-based pick in
       draw_player_skin_mannequin), 1..5 = an explicit MANNEQUIN/STAN/MIKE/LEELA/GEORGE choice,
       kept in sync BY HAND with internal/shankpit.AIKit* (IDUNA) and this same file's own
       draw_player_skin_mannequin kits[5] array order -- same class of manual cross-boundary sync
       AIRole's own comment above already accepts. */
    int forced_kit;
    /* third_person -- S536, the BIG_O<->SHANKPIT/PAPERCRAFT engine-merge track's own continuation
       past the MODE_TYLER demo pass. That pass proved PAPERCRAFT's real orbit-camera formula
       (PAPERCRAFT/apps/client/src/main.c) ported cleanly into this engine's pre-existing cx/cz/
       cam_y third-person mechanism (already live for vehicles/death-cam), but gated the whole
       thing on "MODE_TYLER && forced_kit==AI_KIT_LEELA" -- a demo-shaped hack, not a real engine
       capability, since it hard-coded BOTH a specific game mode AND a specific character skin as
       the trigger for a genuinely mode-agnostic, skin-agnostic camera choice. This field is the
       fix: a real, standalone, server-authoritative third-person toggle any mode can set on any
       player independent of forced_kit, matching forced_kit's own "0 = auto/off" convention (0 =
       first person, 1 = third person). tyler_apply_phase_override (apps/server/src/main.c) is
       still the only real caller this pass (the Duck phase sets it alongside, not instead of,
       forced_kit=AI_KIT_LEELA) -- the real target consumer is MODE_STORY's own not-yet-built
       night/social-stealth register (BIGO_ENGINE_MERGE_NORTHSTAR.md's own "night" phase), which can
       now set this flag directly whenever it lands, without needing a third camera branch. */
    int third_person;
    /* card #487 (survival guns as world items): when weapon_gated, only weapons whose bit is set in
       weapon_owned_mask can be equipped (local_update). 0/0 = every weapon, i.e. all other modes, unchanged. */
    int weapon_gated;
    unsigned int weapon_owned_mask;
} PlayerState;

typedef struct {
    int active; unsigned int timestamp;
    float x, y, z;
    float vx, vy, vz;
} LagRecord;

typedef struct {
    float forward;
    float yaw;
    float strafe;
    int ascend;
    int descend;
} HeliInputState;

typedef struct {
    int active;
    int id;
    int scene_id;
    float x, y, z;
    float vx, vy, vz;
    float yaw;
    float pitch_visual;
    float roll_visual;
    float rotor_angle;
    float rotor_speed;
    float collective;
    int health;
    int occupant_player_id;
    int grounded;
    HeliInputState input;
    /* flight-model state (heli_rules.prn via net_sim.h); server/local only, not on the wire -- the
       clients draw pitch_visual/roll_visual, which are derived from these */
    float att_pitch, att_roll;   /* degrees, + = nose down / banked right */
    float yaw_rate;              /* degrees per tick, + = turning right */
} HelicopterState;

typedef struct {
    int active;
    int id;
    int scene_id;
    float x, y, z;
    float vx, vy, vz;
    float yaw;
    float pitch;
    float roll;
    float steer;
    float throttle;
    int health;
    int occupant_player_id;
    int grounded;
    /* extrapolation anchor — not serialised, local client only */
    float snap_x, snap_y, snap_z;
    float snap_vx, snap_vy, snap_vz;
    float snap_yaw;
    float snap_yaw_rate_dps; /* deg/s measured from consecutive snapshots */
    unsigned int snap_recv_ms;
} BuggyState;

/* Icelandic horse — Toledo mount. Tölt gait: 4-beat amble, smooth at speed.
 * No gallop bounce; good on rough terrain. Cannot fire weapons while mounted.
 * Named for the TYLER archive: Tyler rode this breed in Toledo, 1040–1051 CE.
 * Speed cap: 8 m/s (tölt); acceleration: smooth (no gear shift). */
typedef struct {
    int  active;
    int  id;
    int  scene_id;
    float x, y, z;
    float vx, vy, vz;
    float yaw;
    float speed;          /* current scalar speed along yaw direction */
    float tolt_phase;     /* 0..1, cycles once per stride — drives hoof audio */
    int  occupant_player_id;
    int  grounded;
    int  health;
} HorseState;

typedef enum {
    FLAG_AT_HOME = 0,
    FLAG_CARRIED = 1,
    FLAG_DROPPED = 2
} FlagState;

typedef struct {
    int owning_team_id;
    int scene_id;
    float home_x, home_y, home_z;
    float x, y, z;
    int state;
    int carrier_id;
    unsigned int dropped_until_ms;
    unsigned int last_interaction_ms;
} CtfFlagState;

typedef struct {
    int active;
    int scene_id;
    int score_limit;
    int capture_scores[2];
    CtfFlagState flags[2];
    unsigned int event_counter;
} CtfMatchState;

/* MODE_QUEUE=108 -- S459-34, founder real-time: "set up a bot queue... 3 bots - 4 player games...
 * network queue... architect the bots the same way the bots work for brawlpit... packet level
 * bots just like brawlpit." A real, networked, non-team free-for-all mode (same generic non-TDMO
 * connect path MODE_DEATHMATCH/MODE_CTF already use -- see server_handle_packet, no team
 * assignment needed) whose population is filled by REAL packet-level bot client PROCESSES (see
 * apps2/emily-bot, launched via ops/shankpit-bot-pool.sh), not an in-process PlayerState puppet
 * the way MODE_TDMO's own tdmo_spawn_bot_on_team is -- the server never drives a QUEUE bot's
 * inputs itself; a QUEUE bot connects, sends real PacketUserCmd, and is completely
 * indistinguishable from a real human client on the wire, matching BRAWLPIT's own real
 * rl_bot_pool.py precedent (a standing pool of real UDP clients queuing through the same real
 * matchmaker packets humans use) more literally than BRAWLPIT's own ORDINARY matchmaker bot-fill
 * (which is actually the same in-process-puppet pattern TDMO already uses here). */
/* MODE_ZOMBIES=110 -- founder real-time, 2026-10-02: ZOMBIES is the basic sandbox (replaces the
 * FIND CTF menu tile). A persistent level (default: nextown) with a real day/night lifecycle
 * (story_clock) driving a witness_ai population -- citizens by day, zombies rising at dusk and
 * burning off at dawn, The Men cleaning up witnesses. Destruction (brick damage) is saved back to
 * the IDUNA "zombies" level collection. See witness_ai_zombies_tick. Uses the story-mode plumbing
 * MODE_TYLER already borrows (story_phase PLAYING, i>0 corpses never respawn). */
/* MODE_TYLER=109 -- TYLER VALHANNA cold open (episodes/vh01_valhanna_coldopen.md), founder
 * real-time: "bring it to life with the shankpit engine... write a new game mode called TYLER...
 * this is a demo for BIG_O" (S536 BIG_O<->SHANKPIT engine merge track, docs2/specs/
 * BIGO_ENGINE_MERGE_NORTHSTAR.md). Two real, distinct phases sharing one game mode number (the
 * level transition between them is what actually changes what the player controls, not the mode
 * itself -- same "mode stays constant, level/state changes" shape MODE_STORY already uses across
 * VOXWORLD vs MODE_STORY_CAVE's own separate mode number, except here both phases deliberately
 * share MODE_TYLER since neither is a combat encounter needing its own bot-fill/scoring rules):
 * phase 1 (level tyler_1986_iceland) -- the player is a spectator-state "wisp" (see
 * STATE_SPECTATOR below) watching an AI_MODE_SCRIPTED Tyler+Hana sequence; phase 2 (level
 * construct) -- the player controls the Duck (gband_skel_npc "Leela" kit) in third person. See
 * packages/simulation/tyler_coldopen.h for the scripted-sequence coordinator. */
typedef enum { MODE_DEATHMATCH=0, MODE_TDM=1, MODE_SURVIVAL=2, MODE_CTF=3, MODE_ODDBALL=4, MODE_LOCAL=98, MODE_NET=99, MODE_EVOLUTION=100, MODE_TDMB=101, MODE_TDMO=102, MODE_CTFB=103, MODE_CTFO=104, MODE_STORY=105, MODE_HEADED_BOT=106, MODE_STORY_CAVE=107, MODE_QUEUE=108, MODE_TYLER=109, MODE_ZOMBIES=110 } GameMode;
typedef enum {
    STORY_PHASE_CUTSCENE = 0,    /* intro cutscene (TYLER episode — Tyler arrives) */
    STORY_PHASE_PLAYING = 1,
    STORY_PHASE_RIFT_OPENING = 2,
    STORY_PHASE_SWARM = 3,
    STORY_PHASE_AFTER_SWARM = 4,
    STORY_PHASE_FAILED = 5,
    STORY_PHASE_COMPLETE = 6,
    STORY_PHASE_OUTRO = 7        /* outro cutscene (Tyler files zero-reading observation) */
} StoryPhase;

#define STORY_MAX_SWARM_ENEMIES 24

typedef enum {
    STORY_ENEMY_NONE = 0,
    STORY_ENEMY_RIFT_HOUND = 1,
    STORY_ENEMY_SHAMBLER_TROOPER = 2,
    STORY_ENEMY_GORE_BRUTE = 3
} StoryEnemyType;

/* ── Mechanism Reader — TYLER lore tool ───────────────────────────────
 * Tyler's archive mechanism measures Goetia frequencies at sites.
 * In story mode the mechanism ticks continuously, producing a Hz reading
 * and a site taxonomy class.  When the boss enters question-state the
 * reading collapses to zero (the mechanism cannot read itself).
 *
 * Site taxonomy mirrors the TYLER archive classification system:
 *   UNCLASSIFIED → baseline, no pattern detected
 *   DWELLING / WITNESS / FAREWELL / PASSAGE / LIMINAL → standard types
 *   ZERO_TAXONOMY → the cave phenomenon; mechanism returns nothing
 */
typedef enum {
    SITE_CLASS_UNCLASSIFIED = 0,
    SITE_CLASS_DWELLING,
    SITE_CLASS_WITNESS,
    SITE_CLASS_FAREWELL,
    SITE_CLASS_PASSAGE,
    SITE_CLASS_LIMINAL,
    SITE_CLASS_ZERO_TAXONOMY   /* mechanism returns no reading */
} SiteClass;

typedef struct {
    float     signal_hz;          /* current Goetia frequency (Stolas baseline 7.83) */
    SiteClass site_class;         /* derived taxonomy from frequency pattern          */
    int       question_state;     /* 1 = question-state active (boss phase)           */
    int       zero_reading;       /* 1 = mechanism returning nothing                  */
    float     archive_pressure;   /* 0..1 — how much the site is "being recorded"     */
    unsigned int last_tick_ms;
} MechanismReading;

/* Boss question-state phase:
 * - Triggered at 33% health
 * - Boss becomes semi-invulnerable (10% damage only)
 * - Mechanism reading collapses to zero
 * - Resolved when player reaches observation_x/z (the rift core)
 * - Once resolved, boss fully vulnerable again and mechanism rereads
 */
typedef struct {
    int active;
    int defeated;
    int indestructible;           /* 1 = cannot be killed (CAVE-001 entity) */
    float x, y, z;
    float yaw;
    float health;
    float max_health;
    unsigned int last_attack_ms;
    unsigned int hurt_flash_until_ms;
    /* question-state fields */
    int question_state;           /* 0=off 1=active 2=resolved */
    float observation_x, observation_z; /* point player must reach */
    unsigned int question_entered_ms;
    unsigned int question_resolved_ms;
} StoryBossState;

typedef struct {
    int active;
    int type;
    float x, y, z;
    float vx, vy, vz;
    float yaw;
    float radius;
    float height;
    float health;
    float max_health;
    float speed;
    float attack_radius;
    int damage;
    unsigned int spawn_ms;
    unsigned int last_attack_ms;
    unsigned int hurt_flash_until_ms;
    unsigned int death_ms;
    int dying;
} StoryEnemy;

typedef struct {
    int active;
    float x, y, z;
    float yaw;
    float radius;
    unsigned int opened_ms;
    unsigned int next_spew_ms;
    int spew_count;
    int wave_spawned;
    unsigned int pulse_seed;
    unsigned int last_spew_ms;
} StoryRiftState;

typedef struct {
    PlayerState players[MAX_CLIENTS];
    Projectile projectiles[MAX_PROJECTILES];
    HelicopterState helicopters[MAX_HELICOPTERS];
    BuggyState buggies[MAX_BUGGIES];
    HorseState horses[MAX_HORSES];
    LagRecord history[MAX_CLIENTS][LAG_HISTORY];
    int server_tick;
    int game_mode;
    int scene_id;
    int pending_scene;
    int transition_timer;
    int team_scores[2];
    int score_limit;
    int match_over;
    int winning_team;
    int story_phase;
    unsigned int story_phase_start_ms;
    StoryBossState story_boss;
    StoryRiftState story_rift;
    StoryEnemy story_swarm[STORY_MAX_SWARM_ENEMIES];
    MechanismReading mechanism;   /* TYLER archive mechanism — frequency reader */
    unsigned int cave_endure_until_ms; /* CAVE-001: endurance win condition timestamp */
    DayNightClock story_clock; /* EMILY/BACKLOG.md SECTION 536 follow-up ("server-authoritative
        day/night sync") -- authoritative for MODE_STORY/MODE_STORY_CAVE only (local_init_match
        seeds it, apps/server's own per-tick loop and local_game.h's local_update both advance
        it); every other mode ticks its own client-local fallback instead, see apps/lobby's own
        render-loop doc comment near its real use. */
    CtfMatchState ctf;
    struct sockaddr_in clients[MAX_CLIENTS];
    ClientMeta client_meta[MAX_CLIENTS];
} ServerState;

#endif
