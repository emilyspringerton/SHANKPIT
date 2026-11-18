#ifndef ASCEND_H
#define ASCEND_H

/* ascend.h -- /ascend in SHANKPIT's chat: your WoW guilds' Discords, from the Collection's GuildHerald addon (founder,
 * 2026-11-17: "im in these guilds and the message of the day has the discord info but i dont want to type it in on my
 * wotan account if im connected to discord i should be able to type /ascend in the shankpit terminal to sync my
 * discord from the wow data").
 *
 *   WoW guild MOTDs --(GuildHerald + PARENA scripts/collection-guild-feed.sh)--> COLLECTION_GUILD_FEED
 *     --(this mod: every row into REFLUX, then polled back)--> guilds and their Discord invites
 *   /ascend        lists them        /ascend join   opens up to asc-open-max invite links for you to accept
 *
 * Discord does not let an app put a person into a server (that needs a bot already in it), so joining stays one
 * click in Discord, which also tells you when you are already a member. Every decision is ascend_rules.c (PARENA).
 * COLLECTION_GUILD_FEED has no default: unset, /ascend says how to turn it on. */

#define ASCEND_MAX_GUILDS 64
#define ASCEND_MAX_CODES 8
#define ASCEND_NAME 64
#define ASCEND_LINE 160
#define ASCEND_MAX_NAMES 1024

typedef struct {
    char name[ASCEND_NAME], realm[ASCEND_NAME];
    char codes[ASCEND_MAX_CODES][40];
    int ncodes;
    unsigned char opened[ASCEND_MAX_CODES];
} AscendGuild;

typedef struct {
    const char *path;
    char names[ASCEND_MAX_NAMES + 1][ASCEND_NAME];
    int nnames;
    AscendGuild g[ASCEND_MAX_GUILDS];
    int ng, guilds_total, invites_total, loaded;
    int cursor_abs;
} Ascend;

/* path NULL = COLLECTION_GUILD_FEED from the environment. */
void ascend_init(Ascend *a, const char *path);
/* Read the feed (every row through REFLUX). 1 loaded, 0 no path set, -1 no readable v1 feed there. */
int ascend_load(Ascend *a);
/* Load a feed held in memory (what ascend_load does with the file). 1 loaded, -1 not a v1 feed. */
int ascend_load_text(Ascend *a, const char *text);
/* "/ascend <args>": args "" lists, "join" opens the next invites through open_url (NULL = only list them).
 * Writes up to max lines; tones get chat colours (5 system). Returns lines written. */
int ascend_command(Ascend *a, const char *args, char out[][ASCEND_LINE], int *tones, int max,
                   void (*open_url)(const char *url));

#endif
