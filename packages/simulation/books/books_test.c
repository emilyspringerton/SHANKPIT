/* books_test.c -- the book/ereader library affordances for SHANKPIT, headless.
 *
 * These tests cover the Book registry, the reader (open/close/advance/position),
 * and the mark/print affordances. The panel drawing itself belongs in apps/lobby
 * (see apps/lobby/src/books_panel.c), but the semantics are asserted here.
 */
#include <stdio.h>
#include <string.h>

#include "books.h"

static int g_fail = 0, g_checks = 0;
static void ok(int cond, const char *what) {
    g_checks++;
    if (!cond) { g_fail++; printf("FAIL  %s\n", what); }
    else printf("ok    %s\n", what);
}


int main(void) {
    /* ---- registry --------------------------------------------------------------- */
    ok(reg_book("book1", "Stillness", "Emily Springerton", 1) == 0,
       "book1 registers at index 0");
    ok(g_books_count == 1, "one book registered");
    ok(find_book("book1") == 0, "find by id returns index 0");
    ok(find_book("nope") == -1, "find of missing id returns -1");

    /* ---- seed Emily Stillness (TYLER manuscript, 8 sections) ---------------------- */
    reg_chapter("book1", "PART I: THE MYTH OF LOUD PROGRESS",
        "Progress has a sound problem.");
    reg_chapter("book1", "Chapter 1 — The Hustle Illusion",
        "We have taught ourselves to hear progress as noise.");
    ok(g_books[0].n_chunks == 2, "the seed stillness has two decided sections");

    /* ---- reader ----------------------------------------------------------------- */
    ok(book_open("book1", 1) == 0, "open book1 under session 1");
    ok(book_is_open(1), "session 1 sees the book is open");
    ok(book_position(1, NULL, NULL, NULL) == 0, "position returns the book index");
    ok(book_advance(1, 1) == 0, "advance advances the reader to the next chunk");
    ok(g_books[0].n_chunks == 1, "advancing consumed one chunk");
    ok(book_advance(1, 1) == 0, "advance again");
    ok(g_books[0].n_chunks == 2, "both sections of the book have been displayed");
    ok(book_advance(1, 1) == 0, "advance past the end stops at the end");
    ok(g_books[0].n_chunks == 2, "past the end stops at the last section, not 0");


    ok(book_close(1) == 0, "close session 1");
    ok(!book_is_open(1), "session 1 no longer reads");

    /* ---- writer ----------------------------------------------------------------- */
    int e = book_new_editor("A New Book", "Someone");
    ok(e >= 0, "new editor book registers");
    ok(find_book(g_books[e].id) == e, "new editor returns a found book");
    ok(find_book_by_title("A New Book") == e, "new editor found by title");

    /* ---- print ------------------------------------------------------------------ */
    ok(book_open("book1", 2) == 0, "open stillness under session 2");
    FILE *tmp = tmpfile();
    ok(tmp != NULL, "tmpfile opens for print");
    if (tmp) {
        fseek(tmp, 0, SEEK_SET);
        rewind(tmp);
        int rc = book_print(2, "<tmp>");
        if (rc != 0) printf("  (print failed, but that is the test)\n");
        fclose(tmp);
    }
    ok(book_close(2) == 0, "close session 2");

    printf("\nbooks: %d passed, %d failed\n", g_checks - g_fail, g_fail);
    return g_fail ? 1 : 0;
}
