/* books.h -- the book/ereader library affordances for SHANKPIT.
 *
 * Introduces the Book entity registry: one Book per authored title, a small
 * metadata key/value table (title/author/part/headings), and a tiny reader
 * layer (Ereader) that exposes an open book through the same F-interface the
 * engine already drives with edu_widgets. This mirrors the existing widget
 * rules / widget circuit split exactly.
 *
 * Pattern followed: the "rules" live in this module's own C; hosts (apps/lobby,
 * apps/server) install them, link the generated test, and call the
 * book_{install,reg,meta,read,mark}* functions. A reader panel is drawn by the
 * owning app (see apps/lobby/src/books_panel.c) and wired into the existing
 * widget_circuit code path so it reuses the F-prompt and UP/DOWN/LEFT/RIGHT
 * key contract the founder already has.
 *
 * Seed content ships from TYLER's Emily Stillness manuscript
 * (TYLER/manuscripts/emily_stillness.md, 3536 words, 8 sections). The book is
 * stored as plain text; a Book is decided and read in seconds, not pages, so
 * the reader shows one paragraph per tick -- no paging.
 */
#ifndef SHANKPIT_BOOKS_H
#define SHANKPIT_BOOKS_H

#include <stddef.h>

/* The max number of books a single level can install. */#define BOOKS_MAX 64

/* Up to this many readable sections a single book keeps. */#define BOOKS_MAX_SECTIONS 4096

/* One readable chunk of a book: the text plus the #-heading it came from. */typedef struct {
    const char *text;
    size_t len;
    const char *head;
} BookChunk;

/* One authored book in the registry. */typedef struct {
    char id[32];                 /* unique, set by reg_book */
    char title[64];
    char author[64];
    int part;                    /* which part of the book number we are in */
    int n_chunks;                /* how many chunks this book holds */
    int declared;                /* the total number of chunks declared */
    BookChunk chunk[BOOKS_MAX_SECTIONS];
    int reading;                 /* 0 = idle, 1 = reader open */
    int owner;                   /* the session/player holding it, or -1 */
} Book;

/* The global registry. */extern Book g_books[BOOKS_MAX];
extern int g_books_count;

/* Seed content shipped from TYLER's Emily Stillness manuscript. */#define BOOKS_SEED_EMILY_STILLNESS "seed_emily_stillness"

/* Install the built-in rules. Call once at startup from the host that owns the
 * widget circuit. */void book_install(void);

/* Registry. Returns -1 when the table is full or id is empty/too long. */int reg_book(const char *id, const char *title, const char *author, int part);
int reg_chapter(const char *book_id, const char *head, const char *text);
int reg_chapter_from_toml(const char *book_id, const char *head, const char *toml);
int reg_chapter_file(const char *book_id, const char *head, const char *path);

/* Look up a book. */int find_book(const char *id);
int find_book_by_title(const char *title);

/* Reader: open a book under a session. Returns 0 on success, -1 otherwise. */int book_open(const char *id, int session);
int book_close(int session);
int book_is_open(int session);

/* Advance the reader: the next/previous chunk. Does the whole tick of motion. */int book_advance(int dir, int session);

/* The current reading position in a book (0,0) = nowhere. */int book_position(int session, const char **id_out,
                       int *part_out, int *chunk_out);

/* Editor: open a new book for writing. */int book_new_editor(const char *title, const char *author);

/* Mark a place in the book for the reader's next session. */int book_mark_place(int session);

/* Print the open book to a plain-text file for the level's log. */int book_print(int session, const char *path_out);

#endif /* SHANKPIT_BOOKS_H */
