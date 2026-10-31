/* br_stadium_test.c -- the native stadium, headless: the shape, the normal, the mesh, and the draw
 * loop's vertex count.
 *
 * The property that matters is that ONE set of numbers (PARENA's br_stadium.prn -> br_stadium.c)
 * produces both the surface the car is clamped to and the triangles the player sees. So the test
 * checks the mesh against the SDF rather than against a stored golden vertex list: every emitted
 * vertex must sit ON the surface, and every emitted normal must BE the surface normal. If the two ever
 * drifted apart, a car would drive through a wall it appears to be touching -- which is exactly the
 * class of bug that a copied-from-the-web constant list would hide.
 *
 * No GL, no display: the draw header is exercised against counting stubs, so "the mesh reaches the
 * pipeline" is checked as a count, and nothing here claims anything about how it looks.
 */
#include <stdio.h>
#include <math.h>

static int g_fail = 0;
static int g_checks = 0;

static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) { g_fail++; printf("FAIL  %s\n", what); }
    else printf("ok    %s\n", what);
}

/* ---- GL stubs, so br_stadium_gl.h is compiled and driven for real ------------------------------- */
#define GL_QUADS 0x0007
#define GL_LINES 0x0001
static int g_gl_verts = 0, g_gl_begins = 0;
void glBegin(unsigned int mode) { (void)mode; g_gl_begins++; }
void glEnd(void) {}
void glColor3f(float r, float g, float b) { (void)r; (void)g; (void)b; }
void glNormal3fv(const float *n) { (void)n; }
void glVertex3fv(const float *v) { (void)v; g_gl_verts++; }
void glVertex3f(float x, float y, float z) { (void)x; (void)y; (void)z; g_gl_verts++; }

#include "../render/br_stadium_gl.h"

int main(void) {
    float H[3], C[3];
    br_stadium_dims(H, C);
    printf("stadium: half %.1f x %.1f, height %.1f, fillet %.1f, centre y %.1f\n",
           H[0], H[2], stadium_height(), stadium_radius(), C[1]);

    /* ---- the room is the size PARENA says ------------------------------------------------------- */
    ok(stadium_radius() > 0.0f, "the fillet radius is a real, positive number");
    ok(fabsf(H[0] - H[2]) < 0.01f, "the bowl is square in plan -- the track is a loop, not a long rectangle");
    ok(H[1] > 0.0f, "there is headroom above the shrinking box (the bowl has a lid)");

    /* ---- the SDF agrees with itself ------------------------------------------------------------ */
    {
        float cx = 0.0f, cy = C[1], cz = 0.0f;
        ok(br_stadium_free(cx, cy, cz) > 100.0f, "the middle of the arena is deep in free air");
        ok(br_stadium_open(cx, cy, cz) == 1, "the middle is spawnable");
        /* straight down from the centre: the floor is at -H[1] - radius below the centre */
        float floor_y = C[1] - H[1] - stadium_radius();
        ok(fabsf(br_stadium_free(cx, floor_y, cz)) < 0.05f, "the floor plane is exactly on the surface");
        ok(br_stadium_free(cx, floor_y + 5.0f, cz) > 4.0f, "standing height above it is open air");
        ok(br_stadium_free(cx, floor_y - 5.0f, cz) < -4.0f, "under the floor is solid");
        /* the far wall: floor height, out past half_x + radius */
        float wall_x = H[0] + stadium_radius();
        ok(fabsf(br_stadium_free(wall_x, C[1], cz)) < 0.05f, "the side wall is exactly on the surface");
        ok(br_stadium_free(wall_x + 4.0f, C[1], cz) < 0.0f, "past the wall is solid, not air");
        ok(br_stadium_open(wall_x + 4.0f, C[1], cz) == 0, "and it is not walkable");
        /* the ceiling: the point that makes wall-riding possible */
        float ceil_y = C[1] + H[1] + stadium_radius();
        ok(fabsf(br_stadium_free(cx, ceil_y, cz)) < 0.05f, "the ceiling is a real surface, not a void");
        ok(br_stadium_free(cx, ceil_y - 3.0f, cz) > 0.0f, "and there is air under it to drive on");
    }

    /* ---- normals point out of the wall, into the air ------------------------------------------- */
    {
        float n[3];
        br_stadium_normal(0.0f, C[1] - H[1] - stadium_radius(), 0.0f, n);
        ok(n[1] > 0.95f, "the floor normal points up");
        /* normals follow the SDF gradient, so they point from the wall INTO the room's air --
         * which is the direction a car's fans clamp its up-axis to. */
        br_stadium_normal(H[0] + stadium_radius(), C[1], 0.0f, n);
        ok(n[0] < -0.95f, "the +x wall normal points into the arena (-x)");
        br_stadium_normal(-(H[0] + stadium_radius()), C[1], 0.0f, n);
        ok(n[0] > 0.95f, "the -x wall normal points into the arena (+x)");
        /* inside the fillet: 45 degrees, which is the whole point of a continuous normal */
        float r = stadium_radius();
        /* a point ON the floor-to-wall fillet, halfway round the arc: qx = qy = r/sqrt(2) */
        float px = H[0] + r * 0.70710678f;
        float py = C[1] - H[1] - r * 0.70710678f;
        printf("      fillet probe: free = %.4f (on-surface), at (%.1f, %.1f)\n",
               br_stadium_free(px, py, 0.0f), px, py);
        br_stadium_normal(px, py, 0.0f, n);
        ok(n[0] < -0.69f && n[1] > 0.69f && fabsf(n[0] + n[1]) < 0.02f,
           "the fillet's normal is a clean 45 degrees, so the curl is drivable");
        ok(fabsf(sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]) - 1.0f) < 0.001f,
           "the normal is unit length");
    }

    /* ---- the mesh is the surface -------------------------------------------------------------- */
    {
        BrStadiumMesh m;
        int quads = br_stadium_build_mesh(4, &m);
        ok(quads == 6 * 16 + 12 * 16 + 8 * 16, "quality 4 emits 416 quads (faces + edges + corners)");
        ok(m.count == quads, "the count matches what was filled");

        float worst = 0.0f, worst_normal = 1.0f;
        for (int i = 0; i < m.count; i++) {
            for (int k = 0; k < 4; k++) {
                const BrStadiumVert *v = &m.q[i].v[k];
                float d = fabsf(br_stadium_free(v->p[0], v->p[1], v->p[2]));
                if (d > worst) worst = d;
                float n[3];
                br_stadium_normal(v->p[0], v->p[1], v->p[2], n);
                float dot = n[0] * v->n[0] + n[1] * v->n[1] + n[2] * v->n[2];
                if (dot < worst_normal) worst_normal = dot;
            }
        }
        printf("      worst vertex offset from the SDF: %.4f, worst normal dot: %.4f\n", worst, worst_normal);
        ok(worst < 0.05f, "every vertex sits on the surface the car collides with");
        ok(worst_normal > 0.999f, "every stored normal is the surface normal");

        int higher = br_stadium_build_mesh(6, &m);
        ok(higher == 26 * 36, "quality 6 emits 936 quads");
        ok(higher <= BR_STADIUM_MAX_QUADS, "and fits inside the pool");
        ok(br_stadium_build_mesh(0, &m) > 0, "quality 0 is clamped to 1, never zero geometry");
        br_stadium_build_mesh(64, &m);
        ok(m.count == BR_STADIUM_MAX_QUADS, "quality 64 fills the pool instead of overrunning it");
    }

    /* ---- grip and the facet shell ------------------------------------------------------------- */
    ok(br_stadium_grip_permille(0) >= 0 && br_stadium_grip_permille(100000) <= 1000,
       "grip stays inside 0..1000 permille at both ends of the speed range");
    {
        int slow = br_stadium_grip_permille(100);
        int fast = br_stadium_grip_permille(2000);
        printf("      grip at 0.1 units/tick: %d permille, at 2.0: %d, at 6.0: %d\n",
               slow, fast, br_stadium_grip_permille(6000));
        ok(slow < fast, "a crawling car is held far more weakly than a fast one");
        ok(br_stadium_grip_permille(2000) == 1000 && br_stadium_grip_permille(6000) == 1000,
           "at racing speed the hold is total and stays total (saturating, not runaway)");
        ok(br_stadium_grip_permille(-2000) == br_stadium_grip_permille(2000),
           "and grip does not care which way the car is going");
    }
    ok(stadium_facet_degrees() > 0 && stadium_facet_degrees() <= 45,
       "the destructive shell's facet size is a sane angle");

    /* ---- the draw path actually receives the geometry ----------------------------------------- */
    {
        BrStadiumMesh m;
        br_stadium_build_mesh(4, &m);
        g_gl_verts = 0;
        g_gl_begins = 0;
        br_stadium_gl_draw(&m, 1.0f);
        ok(g_gl_begins == 1 && g_gl_verts == m.count * 4,
           "one GL_QUADS batch, four vertices per quad");
        g_gl_verts = 0;
        br_stadium_gl_draw_guides(1.0f);
        ok(g_gl_verts == 16, "the guide frame is twelve short lines, not a second shell");
        br_stadium_gl_draw(0, 1.0f);
        ok(1, "a null mesh is skipped rather than dereferenced");
    }

    printf("\n%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
