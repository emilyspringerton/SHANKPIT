/* gear_board_test.c -- packages/ui/gear_board.h: parsing the forge's board.tsv, finding it, and reloading it when the
 * forge rewrites it. Build: make test-gear-board */
#include "gear_board.h"
#include <stdio.h>

static int fails = 0, passes = 0;
#define CHECK(c, msg) do { if (c) passes++; else { fails++; printf("FAIL: %s\n", msg); } } while (0)

static const char *BOARD =
    "SHANKPIT_GEAR_BOARD\t1\t1763100000\n"
    "C\tAsh-Forever\tMAGE\t60\tcaster\t9000000\tStormwind\t\t0\t80\t4\t4\t0\t-1\n"
    "G\tAsh-Forever\t1\tHead\tCloth Hat\t\t\n"
    "C\tEmily-Forever\tPALADIN\t42\tstrength\t1300000\tIronforge\tThe Commons\t2\t46\t3\t4\t38\t50\n"
    "G\tEmily-Forever\t1\tHead\tLionheart Helm\t65\t4\n"
    "G\tEmily-Forever\t16\tMain hand\tAshbringer\t\t\n"
    "G\tNobody-Forever\t1\tHead\tLost Hat\t1\t1\n"
    "N\t1000\tAsh-Forever\tbags full\tbag space\t0 of 80 slots free\tMooncloth Bag (16 slots) -- your mail\n"
    "N\t600\tEmily-Forever\trequest\tLinen Cloth x20\t\tAsh-Forever bags x20\n";

int main(void) {
    static GearBoard b;
    memset(&b, 0, sizeof b);
    CHECK(gear_board_parse(&b, "hello\n") == -1 && !b.loaded, "not a board: refused");
    CHECK(gear_board_parse(&b, BOARD) == 0 && b.loaded, "a board parses");
    CHECK(b.generated == 1763100000LL, "its time");
    CHECK(b.nchars == 2, "two characters");
    CHECK(strcmp(b.chars[1].name, "Emily-Forever") == 0 && b.chars[1].level == 42, "name and level");
    CHECK(strcmp(b.chars[1].class_, "PALADIN") == 0 && strcmp(b.chars[1].role, "strength") == 0, "class and role");
    CHECK(b.chars[1].gold == 1300000LL, "gold in copper");
    CHECK(strcmp(b.chars[1].subzone, "The Commons") == 0, "sub-zone");
    CHECK(strcmp(b.chars[0].subzone, "") == 0, "an empty field stays empty, the next one is still read");
    CHECK(b.chars[1].bags_free == 2 && b.chars[1].bags_total == 46 && b.chars[1].bags_worn == 3 && b.chars[1].bag_slots == 4, "bags");
    CHECK(b.chars[0].xp_pct == -1 && b.chars[1].xp_pct == 50, "xp percent, -1 at the cap");
    CHECK(b.ngear == 3, "gear of known characters only (a line for an unknown one is dropped)");
    CHECK(b.gear[1].ch == 1 && b.gear[1].slot == 1 && strcmp(b.gear[1].item, "Lionheart Helm") == 0, "gear by slot");
    CHECK(b.gear[1].ilvl == 65 && b.gear[1].quality == 4, "item level and quality");
    CHECK(b.gear[0].ilvl == -1 && b.gear[0].quality == -1, "unknown item level and quality are -1");
    CHECK(b.nneeds == 2 && b.needs[0].urgency == 1000, "needs, most urgent first as written");
    CHECK(strcmp(b.needs[0].from, "Mooncloth Bag (16 slots) -- your mail") == 0, "who can fill it");
    CHECK(strcmp(b.needs[1].detail, "") == 0 && strcmp(b.needs[1].what, "Linen Cloth x20") == 0, "a need with no detail");

    /* finding and reloading the file */
    const char *path = "/tmp/gear_board_test.tsv";
    remove(path);
    setenv("SHANKPIT_GEAR_BOARD", path, 1);
    memset(&b, 0, sizeof b);
    CHECK(gear_board_poll(&b, 5000) == 0 && !b.loaded, "no file yet: nothing loaded");
    CHECK(strstr(b.status, "NO BOARD YET") != NULL, "and the screen says what to run");
    FILE *f = fopen(path, "wb"); fputs(BOARD, f); fclose(f);
    CHECK(gear_board_poll(&b, 5500) == 0, "checked at most once a second");
    CHECK(gear_board_poll(&b, 6100) == 1 && b.nchars == 2, "loaded once it appears");
    CHECK(gear_board_poll(&b, 7200) == 0, "unchanged: not re-read");
    f = fopen(path, "wb"); fputs("SHANKPIT_GEAR_BOARD\t1\t1\nC\tSolo-Forever\tMAGE\t5\tcaster\t0\tElwynn\t\t10\t16\t0\t4\t0\t10\n", f); fclose(f);
    CHECK(gear_board_poll(&b, 8300) == 1 && b.nchars == 1 && strcmp(b.chars[0].name, "Solo-Forever") == 0,
          "rewritten (a different size): re-read");
    remove(path);
    unsetenv("SHANKPIT_GEAR_BOARD");
    setenv("FORGE_DIR", "/tmp/forge-dir", 1);
    memset(&b, 0, sizeof b);
    gear_board_find_path(&b);
    CHECK(strcmp(b.path, "/tmp/forge-dir/board.tsv") == 0, "FORGE_DIR/board.tsv when no explicit path");

    printf("gear board: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
