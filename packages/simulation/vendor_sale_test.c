/* vendor_sale_test.c -- the AUTHORITY's half of the vending affordance, headless.
 *
 * vendor_serve_test.c proves the state machine: the hold, the cooldown, the ceiling. This proves the WIRING
 * around it, which is the half that used to live only inside apps/server and was therefore compiled and never
 * tested: which truck is the nearest one, that a truck in another scene or a truck that is rolling never sells,
 * and that the cone lands on the AUTHORITATIVE health rather than on a copy.
 *
 * The fake world is built out of the real BuggyState from protocol.h, so the struct the server scans is the
 * struct the wire describes -- not a stand-in that could drift from it.
 *
 * NOT proven: that a sold cone reaches the buyer over the network. That needs two live clients and a buyer
 * who is not at full health (a 25-point heal on a 100/100 client is invisible), so it is named as a limit
 * rather than claimed here.
 */
#include <stdio.h>
#include <string.h>

#include "vendor_serve.h"

static int g_failed, g_checks;
#define CHECK(cond, ...) do { \
    g_checks++; \
    if (!(cond)) { g_failed++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define SCENE 3

static void truck_at(BuggyState *b, int id, int scene, float x, float z, float vx) {
    memset(b, 0, sizeof(*b));
    b->active = 1;
    b->ice_cream = 1;
    b->song = 1;
    b->id = id;
    b->scene_id = scene;
    b->x = x;
    b->z = z;
    b->vx = vx;
    b->vz = 0.0f;
}

/* Tick the authority in 50 ms steps until a cone is applied or the budget runs out. Returns how many ms of
 * standing there it took (0 = no cone), which is the number a player actually feels. */
static int hold(VendorServe *v, int *health, float px, float pz,
                const BuggyState *cars, int n, int budget_ms, int *truck_out) {
    int t = 0;
    while (t < budget_ms) {
        if (vendor_serve_authority_tick(v, health, 100, px, pz, SCENE, cars, n, 50, truck_out))
            return t + 50;
        t += 50;
    }
    return 0;
}

int main(void) {
    printf("== vendor sale (authority) ==\n");
    printf("      window %d units, hold %d ms, heal %d, cooldown %d ms, %d jingles\n",
           icecream_window_units(), icecream_serve_ms(), icecream_serve_heal(),
           icecream_serve_cooldown_ms(), icecream_jingle_count());

    BuggyState cars[4];

    /* ---- 1. a stopped vendor in the buyer's own scene heals the authoritative health, once ---- */
    {
        memset(cars, 0, sizeof(cars));
        truck_at(&cars[0], 0, SCENE, 4.0f, 0.0f, 0.0f);
        VendorServe v; vendor_serve_init(&v);
        int health = 60, truck = VENDOR_SERVE_NONE;
        int took = hold(&v, &health, 0.0f, 0.0f, cars, 4, 5000, &truck);
        printf("      first cone after %d ms, from truck %d: health 60 -> %d\n", took, truck, health);
        CHECK(took >= icecream_serve_ms(), "a cone was handed over after only %d ms", took);
        CHECK(truck == 0, "the cone came from truck %d, not the vendor at hand", truck);
        CHECK(health == 60 + icecream_serve_heal(), "a cone took 60 to %d, not %d",
              health, 60 + icecream_serve_heal());
        CHECK(v.served_count == 1, "the machine counted %d cones", v.served_count);
        printf("PASS: a stopped vendor in the same scene writes the heal to the authority's health\n");
    }

    /* ---- 2. a rolling truck is not serving, however close it is ---- */
    {
        memset(cars, 0, sizeof(cars));
        truck_at(&cars[0], 0, SCENE, 0.5f, 0.0f, 1.0f);     /* at arm's length, moving */
        VendorServe v; vendor_serve_init(&v);
        int health = 60;
        int took = hold(&v, &health, 0.0f, 0.0f, cars, 4, 5000, NULL);
        printf("      rolling vendor at arm's length: %d ms, health %d\n", took, health);
        CHECK(took == 0, "a truck rolling past sold a cone after %d ms", took);
        CHECK(health == 60, "a moving truck healed %d -> %d", 60, health);
        CHECK(vendor_serve_progress(&v) == 0, "the hold banked %d/1000 from a moving truck",
              vendor_serve_progress(&v));
        printf("PASS: 'stopped' is part of the sale, not decoration\n");
    }

    /* ---- 3. a truck in another scene is not the nearest truck ---- */
    {
        memset(cars, 0, sizeof(cars));
        truck_at(&cars[0], 0, SCENE + 1, 0.5f, 0.0f, 0.0f);  /* stopped, close, WRONG SCENE */
        VendorServe v; vendor_serve_init(&v);
        int health = 60, truck = 99;
        int took = hold(&v, &health, 0.0f, 0.0f, cars, 4, 3000, &truck);
        printf("      vendor one scene away: %d ms, truck %d, health %d\n", took, truck, health);
        CHECK(took == 0, "a vendor in scene %d sold to a player in scene %d", SCENE + 1, SCENE);
        CHECK(truck == VENDOR_SERVE_NONE, "the out-of-scene truck was reported as nearest (%d)", truck);
        CHECK(health == 60, "an out-of-scene vendor healed somebody");
        printf("PASS: the scene filter is in the scan, not in the caller's head\n");
    }

    /* ---- 4. too far is not at the window ---- */
    {
        memset(cars, 0, sizeof(cars));
        int far_units = icecream_window_units() + 20;
        truck_at(&cars[0], 0, SCENE, (float)far_units, 0.0f, 0.0f);
        VendorServe v; vendor_serve_init(&v);
        int health = 60;
        int took = hold(&v, &health, 0.0f, 0.0f, cars, 4, 3000, NULL);
        printf("      stopped vendor %d units away (window is %d): %d ms, health %d\n",
               far_units, icecream_window_units(), took, health);
        CHECK(took == 0, "a vendor %d units away sold a cone", far_units);
        CHECK(health == 60, "a vendor across the road healed somebody");
        printf("PASS: reach is the window, not the scene\n");
    }

    /* ---- 5. with two vendors, the NEAREST one is the one that serves ---- */
    {
        memset(cars, 0, sizeof(cars));
        truck_at(&cars[0], 1, SCENE, 6.0f, 0.0f, 0.0f);
        truck_at(&cars[1], 2, SCENE, 2.0f, 0.0f, 0.0f);      /* the near one, further down the array */
        VendorServe v; vendor_serve_init(&v);
        int health = 60, truck = VENDOR_SERVE_NONE;
        int took = hold(&v, &health, 0.0f, 0.0f, cars, 4, 3000, &truck);
        printf("      two vendors in scene: served by truck %d after %d ms\n", truck, took);
        CHECK(took >= icecream_serve_ms(), "the nearer vendor did not serve");
        CHECK(truck == 2, "served by truck %d instead of the nearer truck 2", truck);
        printf("PASS: the nearest vendor in the scene is the one you are standing at\n");
    }

    /* ---- 6. one cone per cooldown, on the health the authority holds ---- */
    {
        memset(cars, 0, sizeof(cars));
        truck_at(&cars[0], 0, SCENE, 3.0f, 0.0f, 0.0f);
        VendorServe v; vendor_serve_init(&v);
        /* 40, not 60: two unclamped cones have to fit under the 100 ceiling, or the second sale would be
           measured against the clamp instead of against the heal -- which is what this block is about. */
        int health = 40;
        int first = hold(&v, &health, 0.0f, 0.0f, cars, 4, 3000, NULL);
        CHECK(first > 0, "the first cone never arrived");
        CHECK(health == 40 + icecream_serve_heal(), "the first cone took 40 to %d, not %d",
              health, 40 + icecream_serve_heal());
        int after_first = health;
        printf("      first cone: health %d; standing through the cooldown...\n", after_first);

        /* standing at the same window through the cooldown must change nothing */
        int during = 0;
        for (int t = 0; t < icecream_serve_cooldown_ms() - 300; t += 100)
            during += vendor_serve_authority_tick(&v, &health, 100, 0.0f, 0.0f, SCENE, cars, 4, 100, NULL);
        CHECK(during == 0, "%d cone(s) were sold inside the cooldown", during);
        CHECK(health == after_first, "the cooldown healed %d -> %d", after_first, health);

        /* and after it expires the window serves again, on top of what the first cone left */
        int again = hold(&v, &health, 0.0f, 0.0f, cars, 4, icecream_serve_cooldown_ms() + 4000, NULL);
        printf("      after the cooldown: %d ms more, health %d -> %d\n", again, after_first, health);
        CHECK(again > 0, "the window never reopened after its cooldown");
        CHECK(health == after_first + icecream_serve_heal(),
              "a second cone took %d to %d, not %d", after_first, health,
              after_first + icecream_serve_heal());
        printf("PASS: one cone per cooldown, and the second one is worth what the first was\n");
    }

    /* ---- 7. a cone never pushes a player past the ceiling ---- */
    {
        memset(cars, 0, sizeof(cars));
        truck_at(&cars[0], 0, SCENE, 3.0f, 0.0f, 0.0f);
        VendorServe v; vendor_serve_init(&v);
        int health = 90;                                     /* one cone from the top */
        int took = hold(&v, &health, 0.0f, 0.0f, cars, 4, 3000, NULL);
        CHECK(took > 0, "no cone arrived near the ceiling");
        CHECK(health == 100, "a cone at 90 health made it %d", health);
        printf("PASS: a cone clamps at the player's ceiling (%d), never past it\n", health);
    }

    /* ---- 8. a caller with no health pointer cannot spend anything (the client's own path) ---- */
    {
        memset(cars, 0, sizeof(cars));
        truck_at(&cars[0], 0, SCENE, 3.0f, 0.0f, 0.0f);
        VendorServe v; vendor_serve_init(&v);
        int client_health = 60;                              /* this caller's own copy, never handed over */
        int served = 0;
        for (int t = 0; t < icecream_serve_ms() + 300; t += 50)
            served += vendor_serve_authority_tick(&v, NULL, 0, 0.0f, 0.0f, SCENE, cars, 4, 50, NULL);
        printf("      client-side tick: served %d, its own health copy still %d\n", served, client_health);
        CHECK(served == 1, "the client's own tick did not see the hand-over (%d)", served);
        CHECK(client_health == 60, "a NULL-health caller still changed a health value (%d)", client_health);
        CHECK(v.served_count == 1, "the client counted %d cones", v.served_count);
        printf("PASS: the shared machine tells a client WHEN, and only the authority changes health\n");
    }

    /* ---- 9. whose turn it is: the park window is staggered by slot ---- */
    {
        int cycle = icecream_stop_cycle_ms(), dwell = icecream_dwell_ms();
        int phase = icecream_park_phase_ms(), n = icecream_jingle_count();
        printf("      %d trucks, %d ms per shift, %d ms parked, %d ms cycle\n", n, phase, dwell, cycle);
        CHECK(phase > 0, "a shift is %d ms", phase);
        CHECK(phase * n <= cycle, "%d trucks x %d ms of shift does not fit a %d ms cycle", n, phase, cycle);

        int always_open = 1, one_at_a_time = 1;
        for (int t = 0; t < cycle; t += 250) {
            int parked = 0;
            for (int s = 0; s < n; s++) parked += icecream_parked_for(t, s);
            if (parked == 0) always_open = 0;
            if (parked > 1) one_at_a_time = 0;
            CHECK(icecream_parked_for(t, 0) == icecream_parked(t),
                  "slot 0 parked differently from the plain window at t=%d", t);
        }
        /* Both are properties of the NUMBERS, so a change to the rules keeps this test honest instead of
           failing it: overlapping windows need a shift shorter than the dwell, and a gap needs one longer. */
        if (phase >= dwell) CHECK(one_at_a_time, "two vendors were parked at the same moment");
        if (phase <= dwell) CHECK(always_open, "a district with %d vendors had no window open at some point", n);
        printf("      at most one parked at a time: %s, a window always open: %s\n",
               one_at_a_time ? "yes" : "no", always_open ? "yes" : "no");

        CHECK(icecream_parked_for(0, 0) == 1, "the first truck was not parked at t=0");
        CHECK(icecream_parked_for(0, 1) == 0, "the second truck parked with the first");
        int turns = 0;
        for (int s = 0; s < n; s++) {
            int got = 0;
            for (int t = 0; t < cycle; t += 100) got += icecream_parked_for(t, s);
            if (got > 0) turns++;
        }
        CHECK(turns == n, "only %d of %d vendors ever got a turn", turns, n);
        CHECK(icecream_parked_for(-10, 0) == icecream_parked_for(0, 0),
              "a negative clock parked slot 0 differently");
        CHECK(icecream_parked_for(1000, n) == icecream_parked_for(1000, 0),
              "slot %d did not wrap to slot 0", n);
        printf("PASS: the trucks take turns pulling over instead of parking together\n");
    }

    printf("\n%s%d check(s) failed\n", g_failed ? "FAIL: " : "PASS: ", g_failed);
    return g_failed ? 1 : 0;
}
