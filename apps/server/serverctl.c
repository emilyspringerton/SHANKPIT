// apps/server/serverctl.c
// FARTHQ Server Control Console
// Single-file C99 + ncurses

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

typedef struct {
    int running;
    pid_t pid;
    int port;
} GameInstance;

GameInstance game = {0};

void spawn_game() {
    if (game.running) return;

    pid_t pid = fork();
    if (pid == 0) {
        // Child: launch server
        execl(
            "./brawlpit_server",
            "brawlpit_server",
            "--deathmatch",
            NULL
        );
        // If execl fails
        exit(1);
    }

    game.running = 1;
    game.pid = pid;
    game.port = 6969;
}

void kill_game() {
    if (!game.running) return;

    kill(game.pid, SIGTERM);
    waitpid(game.pid, NULL, 0);

    game.running = 0;
    game.pid = -1;
}

void draw_ui(int selected) {
    clear();

    mvprintw(1, 2, "SHANKPIT SERVER CONTROL");
    mvprintw(2, 2, "shankpit.okemily.com (authoritative, S459-36)");

    mvprintw(4, 2, "Active Game:");
    if (game.running) {
        mvprintw(5, 4, "DM | RUNNING | PID %d | PORT %d", game.pid, game.port);
    } else {
        mvprintw(5, 4, "No active game");
    }

    mvprintw(8, 2, "Actions:");

    if (selected == 0) attron(A_REVERSE);
    mvprintw(9, 4, "Spawn Deathmatch");
    if (selected == 0) attroff(A_REVERSE);

    if (selected == 1) attron(A_REVERSE);
    mvprintw(10, 4, "Kill Game");
    if (selected == 1) attroff(A_REVERSE);

    if (selected == 2) attron(A_REVERSE);
    mvprintw(11, 4, "Quit");
    if (selected == 2) attroff(A_REVERSE);

    mvprintw(14, 2, "Arrow Keys + Enter | tmux-safe | ncurses");

    refresh();
}

int main() {
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, 1);
    timeout(100);

    int selected = 0;
    int running = 1;

    while (running) {
        draw_ui(selected);

        int ch = getch();
        switch (ch) {
            case KEY_UP:
                selected = (selected + 2) % 3;
                break;
            case KEY_DOWN:
                selected = (selected + 1) % 3;
                break;
            case '\n':
                if (selected == 0) spawn_game();
                if (selected == 1) kill_game();
                if (selected == 2) running = 0;
                break;
            case 'q':
            case 'Q':
                running = 0;
                break;
            default:
                break;
        }

        // Reap dead child if it crashed
        if (game.running) {
            int status;
            pid_t result = waitpid(game.pid, &status, WNOHANG);
            if (result == game.pid) {
                game.running = 0;
            }
        }
    }

    kill_game();
    endwin();
    return 0;
}

