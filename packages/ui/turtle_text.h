#ifndef SHANKPIT_TURTLE_TEXT_H
#define SHANKPIT_TURTLE_TEXT_H

#include <math.h>
#include <string.h>
#include <SDL2/SDL_opengl.h>

typedef struct {
    float x;
    float y;
    float heading_deg;
    float scale;
    int pen_down;
} TurtlePen;

static inline TurtlePen turtle_pen_create(float x, float y, float scale) {
    TurtlePen pen;
    pen.x = x;
    pen.y = y;
    pen.heading_deg = 0.0f;
    pen.scale = scale;
    pen.pen_down = 1;
    return pen;
}

static inline void turtle_pen_up(TurtlePen *pen) { pen->pen_down = 0; }
static inline void turtle_pen_down(TurtlePen *pen) { pen->pen_down = 1; }

static inline void turtle_turn(TurtlePen *pen, float deg) {
    pen->heading_deg += deg;
}

static inline void turtle_forward(TurtlePen *pen, float dist) {
    float rad = pen->heading_deg * 0.017453292f;
    float nx = pen->x + cosf(rad) * dist;
    float ny = pen->y + sinf(rad) * dist;
    if (pen->pen_down) {
        glBegin(GL_LINES);
        glVertex2f(pen->x, pen->y);
        glVertex2f(nx, ny);
        glEnd();
    }
    pen->x = nx;
    pen->y = ny;
}

static inline void turtle_move_to(TurtlePen *pen, float x, float y) {
    pen->x = x;
    pen->y = y;
}

static inline void turtle_line_to(TurtlePen *pen, float x, float y) {
    if (pen->pen_down) {
        glBegin(GL_LINES);
        glVertex2f(pen->x, pen->y);
        glVertex2f(x, y);
        glEnd();
    }
    pen->x = x;
    pen->y = y;
}

static inline void turtle_draw_rect(float x, float y, float w, float h) {
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

static inline const unsigned char *turtle_glyph_bitmap(char c) {
    static const unsigned char glyphs[96][7] = {
        {0,0,0,0,0,0,0}, // space
        {4,4,4,4,0,0,4}, // !
        {10,10,0,0,0,0,0}, // "
        {10,31,10,10,31,10,0}, // #
        {4,15,20,14,5,30,4}, // $
        {24,25,2,4,8,19,3}, // %
        {12,18,20,8,21,18,13}, // &
        {6,4,8,0,0,0,0}, // '
        {2,4,8,8,8,4,2}, // (
        {8,4,2,2,2,4,8}, // )
        {0,4,21,14,21,4,0}, // *
        {0,4,4,31,4,4,0}, // +
        {0,0,0,0,0,4,8}, // ,
        {0,0,0,31,0,0,0}, // -
        {0,0,0,0,0,6,6}, // .
        {1,2,4,8,16,0,0}, // /
        {14,17,19,21,25,17,14}, // 0
        {4,12,4,4,4,4,14}, // 1
        {14,17,1,2,4,8,31}, // 2
        {14,17,1,6,1,17,14}, // 3
        {2,6,10,18,31,2,2}, // 4
        {31,16,30,1,1,17,14}, // 5
        {6,8,16,30,17,17,14}, // 6
        {31,1,2,4,8,8,8}, // 7
        {14,17,17,14,17,17,14}, // 8
        {14,17,17,15,1,2,12}, // 9
        {0,6,6,0,6,6,0}, // :
        {0,6,6,0,6,4,8}, // ;
        {2,4,8,16,8,4,2}, // <
        {0,0,31,0,31,0,0}, // =
        {8,4,2,1,2,4,8}, // >
        {14,17,1,2,4,0,4}, // ?
        {14,17,1,13,21,21,14}, // @
        {14,17,17,31,17,17,17}, // A
        {30,17,17,30,17,17,30}, // B
        {14,17,16,16,16,17,14}, // C
        {30,17,17,17,17,17,30}, // D
        {31,16,16,30,16,16,31}, // E
        {31,16,16,30,16,16,16}, // F
        {14,17,16,23,17,17,14}, // G
        {17,17,17,31,17,17,17}, // H
        {14,4,4,4,4,4,14}, // I
        {7,2,2,2,2,18,12}, // J
        {17,18,20,24,20,18,17}, // K
        {16,16,16,16,16,16,31}, // L
        {17,27,21,21,17,17,17}, // M
        {17,25,21,19,17,17,17}, // N
        {14,17,17,17,17,17,14}, // O
        {30,17,17,30,16,16,16}, // P
        {14,17,17,17,21,18,13}, // Q
        {30,17,17,30,20,18,17}, // R
        {15,16,16,14,1,1,30}, // S
        {31,4,4,4,4,4,4}, // T
        {17,17,17,17,17,17,14}, // U
        {17,17,17,17,17,10,4}, // V
        {17,17,17,21,21,21,10}, // W
        {17,17,10,4,10,17,17}, // X
        {17,17,10,4,4,4,4}, // Y
        {31,1,2,4,8,16,31}, // Z
        {14,8,8,8,8,8,14}, // [
        {16,8,4,2,1,0,0}, // backslash
        {14,2,2,2,2,2,14}, // ]
        {4,10,17,0,0,0,0}, // ^
        {0,0,0,0,0,0,31}, // _
        {8,4,2,0,0,0,0}, // `
        {0,0,14,1,15,17,15}, // a
        {16,16,22,25,17,17,30}, // b
        {0,0,14,16,16,17,14}, // c
        {1,1,13,19,17,17,15}, // d
        {0,0,14,17,31,16,14}, // e
        {6,9,8,28,8,8,8}, // f
        {0,0,15,17,15,1,14}, // g
        {16,16,22,25,17,17,17}, // h
        {4,0,12,4,4,4,14}, // i
        {2,0,6,2,2,18,12}, // j
        {16,16,18,20,24,20,18}, // k
        {12,4,4,4,4,4,14}, // l
        {0,0,26,21,21,17,17}, // m
        {0,0,22,25,17,17,17}, // n
        {0,0,14,17,17,17,14}, // o
        {0,0,30,17,30,16,16}, // p
        {0,0,13,19,15,1,1}, // q
        {0,0,22,25,16,16,16}, // r
        {0,0,15,16,14,1,30}, // s
        {8,8,28,8,8,9,6}, // t
        {0,0,17,17,17,19,13}, // u
        {0,0,17,17,17,10,4}, // v
        {0,0,17,17,21,21,10}, // w
        {0,0,17,10,4,10,17}, // x
        {0,0,17,17,15,1,14}, // y
        {0,0,31,2,4,8,31}, // z
        {2,4,4,8,4,4,2}, // {
        {4,4,4,0,4,4,4}, // |
        {8,4,4,2,4,4,8}, // }
        {0,0,6,9,0,0,0} // ~
    };
    if (c < 32 || c > 126) {
        return glyphs[0];
    }
    return glyphs[c - 32];
}

static inline void turtle_draw_glyph(TurtlePen *pen, char c) {
    const unsigned char *rows = turtle_glyph_bitmap(c);
    float px = pen->x;
    float py = pen->y;
    float cell = pen->scale;
    for (int row = 0; row < 7; row++) {
        unsigned char bits = rows[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (1 << (4 - col))) {
                turtle_draw_rect(px + (col * cell), py - (row * cell), cell, cell);
            }
        }
    }
    pen->x += cell * 6.0f;
}

static inline void turtle_draw_text(TurtlePen *pen, const char *text) {
    for (size_t i = 0; i < strlen(text); i++) {
        turtle_draw_glyph(pen, text[i]);
    }
}

#endif
