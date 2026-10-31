/* coop_host.c -- see coop_host.h. No SDL, no GL: this is launch logic, and the test links it alone. */
#include "coop_host.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <time.h>
#endif

int coop_host_zone_valid(const char *zone) {
    if (!zone || !zone[0]) return 0;
    size_t n = strlen(zone);
    if (n >= COOP_HOST_ZONE_MAX) return 0;
    if (zone[0] == '-') return 0; /* the child would parse it as a flag */
    for (size_t i = 0; i < n; i++) {
        char c = zone[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 c == '_' || c == '-';
        if (!ok) return 0; /* no '.', no '/', no '\': cannot walk out of var/maps/ */
    }
    return 1;
}

int coop_host_port_valid(int port) {
    return port >= 1024 && port <= 65535;
}

int coop_host_config(CoopHostConfig *out, const char *zone, int port) {
    if (!out) return 0;
    const char *z = (zone && zone[0]) ? zone : COOP_HOST_DEFAULT_ZONE;
    int p = (port > 0) ? port : COOP_HOST_DEFAULT_PORT;
    if (!coop_host_zone_valid(z) || !coop_host_port_valid(p)) return 0;
    memset(out, 0, sizeof *out);
    snprintf(out->zone, sizeof out->zone, "%s", z);
    out->port = p;
    return 1;
}

int coop_host_fill_argv(const CoopHostConfig *cfg, char *argv[COOP_HOST_ARG_MAX], char port_buf[8]) {
    if (!cfg || !argv || !port_buf) return 0;
    if (!coop_host_zone_valid(cfg->zone) || !coop_host_port_valid(cfg->port)) return 0;
    snprintf(port_buf, 8, "%d", cfg->port);
    argv[0] = (char *)"shank_server";
    argv[1] = (char *)"--port";
    argv[2] = port_buf;
    argv[3] = (char *)"--dungeon";
    argv[4] = (char *)cfg->zone;
    argv[5] = NULL;
    return 1;
}

int coop_host_port_busy(int port) {
    if (!coop_host_port_valid(port)) return 0;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
#ifdef _WIN32
    static int wsa_ready = 0;
    if (!wsa_ready) {
        WSADATA w;
        if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return 0;
        wsa_ready = 1;
    }
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return 0;
    int rc = bind(s, (struct sockaddr *)&addr, sizeof addr);
    closesocket(s);
    return rc != 0; /* bind failed => someone else holds it */
#else
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return 0;
    int rc = bind(s, (struct sockaddr *)&addr, sizeof addr);
    int busy = (rc != 0);
    close(s);
    return busy;
#endif
}

#ifdef _WIN32
int coop_host_start(const char *server_bin, const CoopHostConfig *cfg, CoopHostResult *out) {
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    if (!server_bin || !server_bin[0] || !cfg) {
        snprintf(out->error, sizeof out->error, "no server binary to start");
        return 0;
    }
    if (coop_host_port_busy(cfg->port)) {
        out->ok = 1;
        out->attached = 1;
        snprintf(out->error, sizeof out->error, "world already running on port %d", cfg->port);
        return 1;
    }
    char cmdline[1400];
    snprintf(cmdline, sizeof cmdline, "\"%s\" --port %d --dungeon %s", server_bin, cfg->port, cfg->zone);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        snprintf(out->error, sizeof out->error, "CreateProcess failed (error %lu)", GetLastError());
        return 0;
    }
    /* Give it a moment to fail on a bad path or a busy port, so the lobby can say something true. */
    DWORD w = WaitForSingleObject(pi.hProcess, 400);
    DWORD code = 0;
    if (w == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &code) && code != STILL_ACTIVE) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        snprintf(out->error, sizeof out->error, "server exited on startup (code %lu)", (unsigned long)code);
        return 0;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    out->ok = 1;
    return 1;
}
#else
int coop_host_start(const char *server_bin, const CoopHostConfig *cfg, CoopHostResult *out) {
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    if (!server_bin || !server_bin[0] || !cfg) {
        snprintf(out->error, sizeof out->error, "no server binary to start");
        return 0;
    }
    if (coop_host_port_busy(cfg->port)) {
        out->ok = 1;
        out->attached = 1;
        snprintf(out->error, sizeof out->error, "world already running on port %d", cfg->port);
        return 1;
    }
    if (access(server_bin, X_OK) != 0) {
        snprintf(out->error, sizeof out->error, "server not found: %s", server_bin);
        return 0;
    }
    char port_buf[8];
    char *argv[COOP_HOST_ARG_MAX];
    if (!coop_host_fill_argv(cfg, argv, port_buf)) {
        snprintf(out->error, sizeof out->error, "invalid zone or port");
        return 0;
    }
    /* argv[0] is execv's path AND the child's own argv[0]; use the real path for both so the
     * process table shows the binary a person can go and find. */
    argv[0] = (char *)server_bin;

    pid_t pid = fork();
    if (pid == 0) {
        execv(server_bin, argv);
        _exit(127); /* execv only returns on failure */
    }
    if (pid < 0) {
        snprintf(out->error, sizeof out->error, "fork failed (errno %d)", errno);
        return 0;
    }

    /* A 127 exit means execv itself failed (bad interpreter, wrong arch); anything else means the
     * server ran and gave up (busy port, unwritable map dir). Either way, report it rather than
     * connecting the player to nothing and leaving them on a black screen. */
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 400000000L;
    nanosleep(&ts, NULL);
    int status = 0;
    pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == pid) {
        if (WIFEXITED(status)) {
            snprintf(out->error, sizeof out->error, "server exited on startup (code %d)", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            snprintf(out->error, sizeof out->error, "server killed on startup (signal %d)", WTERMSIG(status));
        } else {
            snprintf(out->error, sizeof out->error, "server exited on startup");
        }
        return 0;
    }
    out->ok = 1;
    out->pid = (long)pid;
    return 1;
}
#endif

#ifdef SHANKPIT_COOP_HOST_SELFTEST
/* Nothing here: the live check lives in coop_host_test.c so this file stays spawn-only. */
#endif
