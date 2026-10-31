/* reflection_map.h -- the reflection environment, as pixels, with no GL anywhere in it.
 *
 * Split out of reflection.h so the map itself can be tested headlessly: this is the only part with
 * behaviour worth asserting (it must be deterministic, must have a bright sky above a dark ground, and
 * must carry a hot spot for a highlight to travel across), and a machine with no GL context still has
 * every reason to check it.
 *
 * A SPHERE MAP is an image of the world as reflected in a mirror ball: the top of the image is
 * overhead, the bottom is underfoot, the middle is the horizon. The fixed-function driver turns a
 * vertex normal plus the camera basis into a lookup into it, which is why one tiny texture is enough
 * to make a surface read as metal.
 */
#ifndef SHANKPIT_REFLECTION_MAP_H
#define SHANKPIT_REFLECTION_MAP_H

/* Fills `px` with size*size RGBA pixels (4 bytes each), row 0 = overhead. Deterministic: no time, no
 * random, no state -- the same call always produces the same bytes. */
static void reflection_map_build(unsigned char *px, int size) {
    if (!px || size < 2) return;
    for (int y = 0; y < size; y++) {
        float v = (float)y / (float)(size - 1);           /* 0 = overhead, 1 = underfoot */
        for (int x = 0; x < size; x++) {
            float u = (float)x / (float)(size - 1);
            float r, g, b;
            if (v < 0.42f) {                               /* sky */
                float k = v / 0.42f;
                r = 0.28f + 0.10f * k;
                g = 0.42f + 0.12f * k;
                b = 0.62f - 0.06f * k;
            } else if (v < 0.56f) {                        /* horizon: warm low sun */
                float k = (v - 0.42f) / 0.14f;
                r = 0.38f + 0.42f * k;
                g = 0.54f + 0.16f * k;
                b = 0.56f - 0.18f * k;
            } else {                                       /* ground */
                float k = (v - 0.56f) / 0.44f;
                r = 0.20f - 0.12f * k;
                g = 0.21f - 0.13f * k;
                b = 0.24f - 0.15f * k;
            }
            /* two cool vertical streaks, like a lit sign reflected in a window */
            if ((u > 0.18f && u < 0.22f) || (u > 0.74f && u < 0.78f)) {
                if (v > 0.34f && v < 0.62f) { r += 0.16f; g += 0.28f; b += 0.34f; }
            }
            /* a hot spot for a specular hit to travel across */
            {
                float dx = u - 0.34f, dy = v - 0.30f;
                float d2 = dx * dx + dy * dy;
                if (d2 < 0.012f) {
                    float k = 1.0f - d2 / 0.012f;
                    r += 0.55f * k; g += 0.52f * k; b += 0.44f * k;
                }
            }
            if (r > 1.0f) r = 1.0f;
            if (g > 1.0f) g = 1.0f;
            if (b > 1.0f) b = 1.0f;
            {
                int i = (y * size + x) * 4;
                px[i + 0] = (unsigned char)(r * 255.0f);
                px[i + 1] = (unsigned char)(g * 255.0f);
                px[i + 2] = (unsigned char)(b * 255.0f);
                px[i + 3] = 255;
            }
        }
    }
}

#endif /* SHANKPIT_REFLECTION_MAP_H */
