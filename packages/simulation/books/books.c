/* books.c -- the book/ereader library affordances for SHANKPIT.
 *
 * Implements the Book registry and the tiny reader layer (the Ereader) that
 * exposes an open book through the same F-interface the engine already drives.
 * A Book is a named title with a small metadata table (title/author/part) and
 * a sequence of readable chunks (sections/paragraphs). Hosts install these
 * (apps/lobby, apps/server) and call the registry/reader functions; the panel
 * drawing itself is done in the owning app (see
 * apps/lobby/src/books_panel.c), wired into the existing widget_circuit key
 * contract (F USE opens, UP/DOWN scroll, BACK closes).
 */
#include "books.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The registry lives in books.h. */
Book g_books[BOOKS_MAX];
int g_books_count = 0;

/* -------------------- install / rules -------------------- */void book_install(void) {
    /* Built-in rules mirror the circuit rules: nothing is wired here -- the
     * host installs and owns the reader. */}

/* -------------------- registry -------------------- */int reg_book(const char *id, const char *title, const char *author, int part) {
    if (!id || !id[0] || strlen(id) >= sizeof g_books[0].id) return -1;
    if (g_books_count >= BOOKS_MAX) return -1;
    Book *b = &g_books[g_books_count];
    memset(b, 0, sizeof *b);
    b->owner = -1;
    snprintf(b->id, sizeof b->id, "%s", id);
    snprintf(b->title, sizeof b->title, "%s", title ? title : "");
    snprintf(b->author, sizeof b->author, "%s", author ? author : "");
    b->part = part;
    g_books_count++;
    return g_books_count - 1;
}

int reg_chapter(const char *book_id, const char *head, const char *text) {
    if (!book_id || !book_id[0] || !head || !text) return -1;
    int i;
    for (i = 0; i < g_books_count; i++) {
        if (strcmp(g_books[i].id, book_id) == 0) break;
    }
    if (i == g_books_count) return -1;
    Book *b = &g_books[i];
    if (b->n_chunks >= BOOKS_MAX_SECTIONS) return -1;
    b->declared = b->n_chunks + 1;
    b->chunk[b->n_chunks].text = text;
    b->chunk[b->n_chunks].len = strlen(text);
    b->chunk[b->n_chunks].head = head ? head : "";
    b->n_chunks++;
    return 0;
}

int reg_chapter_from_toml(const char *book_id, const char *head, const char *toml) {
    if (!book_id || !book_id[0] || !toml) return -1;
    (void)head;
    int i;
    for (i = 0; i < g_books_count; i++) {
        if (strcmp(g_books[i].id, book_id) == 0) break;
    }
    if (i == g_books_count) return -1;
    char *line = strdup(toml);
    if (!line) return -1;
    char *save = NULL;
    for (char *p = strtok_r(line, "\n", &save); p; p = strtok_r(NULL, "\n", &save)) {
        if (strchr(p, '=')) {
            char *key = p;
            char *eq = strchr(key, '=');
            if (eq) *eq = '\0';
            char *val = eq ? eq + 1 : p;
            while (*val == ' ' || *val == '\t') val++;
            reg_chapter(book_id, key, val ? val : "");
        } else {
            reg_chapter(book_id, p, p);
        }
    }
    free(line);
    return 0;
}

int reg_chapter_file(const char *book_id, const char *head, const char *path) {
    (void)head;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return -1; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    int rc = reg_chapter(book_id, head ? head : "body", buf);
    free(buf);
    return rc;
}

int find_book(const char *id) {
    if (!id || !id[0] || strlen(id) >= sizeof g_books[0].id) return -1;
    for (int i = 0; i < g_books_count; i++) {
        if (strcmp(g_books[i].id, id) == 0) return i;
    }
    return -1;
}

int find_book_by_title(const char *title) {
    if (!title) return -1;
    for (int i = 0; i < g_books_count; i++) {
        if (strcasecmp(g_books[i].title, title) == 0) return i;
    }
    return -1;
}

/* -------------------- reader -------------------- */int book_open(const char *id, int session) {
    int i = find_book(id);
    if (i < 0) return -1;
    Book *b = &g_books[i];
    if (b->owner >= 0 && b->owner != session) return -1;
    b->reading = 1;
    b->owner = session;
    b->n_chunks = 0;
    return 0;
}

int book_close(int session) {
    for (int i = 0; i < g_books_count; i++) {
        if (g_books[i].owner == session) {
            g_books[i].reading = 0;
            g_books[i].owner = -1;
            return 0;
        }
    }
    return -1;
}

int book_is_open(int session) {
    for (int i = 0; i < g_books_count; i++) {
        if (g_books[i].owner == session && g_books[i].reading) return 1;
    }
    return 0;
}

int book_advance(int dir, int session) {
    for (int i = 0; i < g_books_count; i++) {
        Book *b = &g_books[i];
        if (b->owner != session || !b->reading) continue;
        b->n_chunks += dir;
        if (b->n_chunks < 0) b->n_chunks = 0;
        if (b->n_chunks > b->declared) b->n_chunks = b->declared;
        return i;
    }
    return -1;
}

int book_position(int session, const char **id_out, int *part_out, int *chunk_out) {
    for (int i = 0; i < g_books_count; i++) {
        Book *b = &g_books[i];
        if (b->owner == session && b->reading) {
            if (id_out) *id_out = b->id;
            if (part_out) *part_out = b->part;
            if (chunk_out) *chunk_out = b->n_chunks;
            return i;
        }
    }
    return -1;
}

/* -------------------- writer / mark / print -------------------- */int book_new_editor(const char *title, const char *author) {
    char id[32];
    snprintf(id, sizeof id, "book_%04d", g_books_count);
    int i = reg_book(id, title, author, 1);
    if (i < 0) return -1;
    return i;
}

int book_mark_place(int session) {
    (void)session;
    return 0;
}

int book_print(int session, const char *path_out) {
    FILE *f = fopen(path_out, "wb");
    if (!f) return -1;
    for (int i = 0; i < g_books_count; i++) {
        Book *b = &g_books[i];
        if (b->owner == session && b->reading) {
            fprintf(f, "# %s\n# by %s, part %d\n\n", b->title, b->author, b->part);
            for (int c = 0; c < b->n_chunks; c++) {
                fprintf(f, "## %s\n%s\n\n", b->chunk[c].head ? b->chunk[c].head : "section", b->chunk[c].text);
            }
        }
    }
    fclose(f);
    return 0;
}
