/* vmctl: talk to the test VM's QEMU sockets.
 *
 *   vmctl mon  "command"           one monitor command (mon.sock), prints the reply
 *   vmctl type "text"              types text on the VM keyboard via sendkey; \n is Enter
 *   vmctl ser  "text" [seconds]    sends text to the serial console (ser.sock), \n is Enter,
 *                                  prints what comes back for that long (default 2 s).
 *                                  QEMU itself logs the serial console to serial.log.
 *
 * Run from the vm/ directory (the sockets are relative paths).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static int conn(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
    if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a) < 0) {
        fprintf(stderr, "vmctl: %s: %s\n", path, strerror(errno));
        exit(1);
    }
    return fd;
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

/* read until nothing arrives for `idle` s or `total` s pass; out may be NULL */
static size_t drain(int fd, double idle, double total, char *out, size_t cap, FILE *copy)
{
    size_t n = 0;
    double end = now() + total;
    for (;;) {
        double left = end - now();
        if (left <= 0)
            break;
        struct pollfd p = {fd, POLLIN, 0};
        int ms = (int)((idle < left ? idle : left) * 1000);
        if (poll(&p, 1, ms) <= 0)
            break;
        char buf[4096];
        ssize_t r = read(fd, buf, sizeof buf);
        if (r <= 0)
            break;
        if (copy)
            fwrite(buf, 1, r, copy);
        if (out && n + r < cap) {
            memcpy(out + n, buf, r);
            n += r;
        }
    }
    if (out)
        out[n] = 0;
    return n;
}

static void sendall(int fd, const char *s, size_t n)
{
    while (n) {
        ssize_t w = write(fd, s, n);
        if (w <= 0)
            exit(1);
        s += w;
        n -= w;
    }
}

/* the monitor echoes the command and ends each reply with "(qemu) " */
static void mon(int fd, const char *cmd, int print)
{
    char line[512], out[65536];
    snprintf(line, sizeof line, "%s\n", cmd);
    sendall(fd, line, strlen(line));
    drain(fd, 0.4, 5, out, sizeof out, NULL);
    if (!print)
        return;
    char *body = strchr(out, '\n'), *end = body ? strstr(body, "(qemu)") : NULL;
    if (body) {
        if (end)
            *end = 0;
        fputs(body + 1, stdout);
    }
}

static const char *key_name(char c, int *shift)
{
    static char one[2];
    static const struct { char c; const char *k; int shift; } map[] = {
        {' ', "spc", 0}, {'\n', "ret", 0}, {'-', "minus", 0}, {'=', "equal", 0}, {'/', "slash", 0},
        {'.', "dot", 0}, {',', "comma", 0}, {';', "semicolon", 0}, {'\'', "apostrophe", 0},
        {'\\', "backslash", 0}, {'[', "bracket_left", 0}, {']', "bracket_right", 0}, {'`', "grave_accent", 0},
        {'_', "minus", 1}, {'+', "equal", 1}, {'?', "slash", 1}, {'>', "dot", 1}, {'<', "comma", 1},
        {':', "semicolon", 1}, {'"', "apostrophe", 1}, {'|', "backslash", 1}, {'{', "bracket_left", 1},
        {'}', "bracket_right", 1}, {'~', "grave_accent", 1}, {'!', "1", 1}, {'@', "2", 1}, {'#', "3", 1},
        {'$', "4", 1}, {'%', "5", 1}, {'^', "6", 1}, {'&', "7", 1}, {'*', "8", 1}, {'(', "9", 1}, {')', "0", 1},
    };
    for (size_t i = 0; i < sizeof map / sizeof *map; i++)
        if (map[i].c == c) {
            *shift = map[i].shift;
            return map[i].k;
        }
    *shift = c >= 'A' && c <= 'Z';
    one[0] = *shift ? c - 'A' + 'a' : c;
    return one;
}

/* "\n" written as two characters in an argument means Enter */
static char *unescape(const char *s, char enter)
{
    char *o = malloc(strlen(s) + 1), *p = o;
    for (; *s; s++) {
        if (s[0] == '\\' && s[1] == 'n') {
            *p++ = enter;
            s++;
        } else {
            *p++ = *s;
        }
    }
    *p = 0;
    return o;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fputs("vmctl mon \"cmd\" | type \"text\" | ser \"text\" [seconds]\n", stderr);
        return 2;
    }
    if (strcmp(argv[1], "mon") == 0 || strcmp(argv[1], "type") == 0) {
        int fd = conn("mon.sock");
        drain(fd, 0.3, 2, NULL, 0, NULL);       /* banner */
        if (argv[1][0] == 'm') {
            mon(fd, argv[2], 1);
        } else {
            char *t = unescape(argv[2], '\n');
            for (char *c = t; *c; c++) {
                int shift;
                const char *k = key_name(*c, &shift);
                char cmd[64];
                snprintf(cmd, sizeof cmd, "sendkey %s%s", shift ? "shift-" : "", k);
                char line[80];
                snprintf(line, sizeof line, "%s\n", cmd);
                sendall(fd, line, strlen(line));
                drain(fd, 0.03, 0.3, NULL, 0, NULL);
            }
            free(t);
        }
        close(fd);
        return 0;
    }
    if (strcmp(argv[1], "ser") == 0) {
        int fd = conn("ser.sock");
        char *t = unescape(argv[2], '\r');
        for (char *c = t; *c; c++) {
            sendall(fd, c, 1);
            usleep(10000);
        }
        free(t);
        double secs = argc > 3 ? atof(argv[3]) : 2;
        drain(fd, secs, secs, NULL, 0, stdout);
        close(fd);
        return 0;
    }
    fprintf(stderr, "vmctl: unknown command %s\n", argv[1]);
    return 2;
}
