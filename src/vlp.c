/* vlp.c
 *
 * Copyright 2026 Giovanni
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"
#include <stdio.h>
#include "vlp.h"

static const char *PKG_RE_PATTERN = "^(\\[[^]]*\\][ \t]*)+(installed|removed) ([^ ]+) .*$";
static const char *UPGRADE_RE_PATTERN = "^(\\[[^]]*\\][ \t]*)+upgraded ([^ ]+) \\(([^)]+)\\).*$";

// Any 'Running pacman ...' command, not just sync ones (needed for
// --show-hist, since a transaction can be triggered by -R etc. too).
static const char *CMD_ANY_RE_PATTERN = "(\\[[^]]*\\][ \t]*)+Running '(pacman[^']*)'";
// Leading timestamp on the "[ALPM] transaction started" line.
static const char *TX_START_RE_PATTERN = "^\\[([^]]*)\\][ \t]*\\[ALPM\\] transaction started";
// installed/removed/upgraded/downgraded/reinstalled, with version detail.
static const char *HIST_PKG_RE_PATTERN = "^(\\[[^]]*\\][ \t]*)+(installed|removed|upgraded|downgraded|reinstalled) ([^ ]+) \\(([^)]*)\\).*$";

static regex_t pkg_re, upgrade_re;
static regex_t cmd_any_re, tx_start_re, hist_pkg_re;

static void strlist_init(StrList *sl) {
    sl->items = NULL;
    sl->count = 0;
    sl->capacity = 0;
}

static void strlist_push(StrList *sl, const char *s) {
    if (sl->count == sl->capacity) {
        size_t new_cap = sl->capacity ? sl->capacity * 2 : 64;
        char **grown = realloc(sl->items, new_cap * sizeof(char *));
        if (!grown) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        sl->items = grown;
        sl->capacity = new_cap;
    }
    sl->items[sl->count++] = strdup(s);
}

static void strlist_free(StrList *sl) {
    size_t i;
    for (i = 0; i < sl->count; i++)
        free(sl->items[i]);
    free(sl->items);
    sl->items = NULL;
    sl->count = 0;
    sl->capacity = 0;
}

static void ptrlist_init(PtrList *pl) {
    pl->items = NULL;
    pl->count = 0;
    pl->capacity = 0;
}

static void ptrlist_push(PtrList *pl, char *s) {
    if (pl->count == pl->capacity) {
        size_t new_cap = pl->capacity ? pl->capacity * 2 : 64;
        char **grown = realloc(pl->items, new_cap * sizeof(char *));
        if (!grown) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        pl->items = grown;
        pl->capacity = new_cap;
    }
    pl->items[pl->count++] = s;
}

static void ptrlist_free(PtrList *pl) {
    free(pl->items);
    pl->items = NULL;
    pl->count = 0;
    pl->capacity = 0;
}

static void strip_newline(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
}

static int file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

static void read_lines_plain(const char *path, StrList *out) {
    FILE *f;
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;

    f = fopen(path, "r");
    if (!f) return;

    while ((len = getline(&line, &cap, f)) != -1) {
        strip_newline(line);
        strlist_push(out, line);
    }
    free(line);
    fclose(f);
}

static void read_lines_gz(const char *path, StrList *out) {
    gzFile f;
    size_t bufcap = 4096;
    char *buf;

    f = gzopen(path, "rb");
    if (!f) return;

    buf = malloc(bufcap);
    if (!buf) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    for (;;) {
        size_t used = 0;
        buf[0] = '\0';

        for (;;) {
            size_t chunk_len;
            if (bufcap - used < 256) {
                char *grown;
                bufcap *= 2;
                grown = realloc(buf, bufcap);
                if (!grown) {
                    fprintf(stderr, "out of memory\n");
                    exit(1);
                }
                buf = grown;
            }

            if (gzgets(f, buf + used, (int)(bufcap - used)) == NULL) {
                if (used == 0) {
                    goto done;
                }
                break;
            }

            chunk_len = strlen(buf + used);
            used += chunk_len;

            if (used > 0 && buf[used - 1] == '\n')
                break;
        }
        strip_newline(buf);
        strlist_push(out, buf);
    }
done:
    free(buf);
    gzclose(f);
}

static void iter_log_lines(StrList *out) {
    if (file_exists(LOG_PATH_ROTATED)) {
        read_lines_plain(LOG_PATH_ROTATED, out);
    } else if (file_exists(LOG_PATH_ROTATED_GZ)) {
        read_lines_gz(LOG_PATH_ROTATED_GZ, out);
    }
    if (file_exists(LOG_PATH)) {
        read_lines_plain(LOG_PATH, out);
    }
}

static int line_is_install_or_remove(const char *line) {
    return strstr(line, "] installed") != NULL || strstr(line, "] removed") != NULL;
}

static int line_is_upgraded(const char *line) {
    return strstr(line, "] upgraded") != NULL;
}

static int line_is_any_command(const char *line) {
    return strstr(line, "[PACMAN] Running 'pacman") != NULL;
}

static void filter_lines(StrList *in, line_pred pred, PtrList *out) {
    size_t i;
    ptrlist_init(out);
    for (i = 0; i < in->count; i++) {
        if (pred(in->items[i]))
            ptrlist_push(out, in->items[i]);
    }
}

static void ptrlist_last_n(PtrList *in, int n, char ***start, size_t *count) {
    if (n <= 0 || (size_t)n >= in->count) {
        *start = in->items;
        *count = in->count;
    } else {
        *start = in->items + (in->count - (size_t)n);
        *count = (size_t)n;
    }
}

static void compile_regex(regex_t *re, const char *pattern) {
    int rc = regcomp(re, pattern, REG_EXTENDED);
    if (rc != 0) {
        char errbuf[256];
        regerror(rc, re, errbuf, sizeof(errbuf));
        fprintf(stderr, "regex compile error: %s\n", errbuf);
        exit(1);
    }
}

void compile_all_regexes(void) {
    compile_regex(&pkg_re, PKG_RE_PATTERN);
    compile_regex(&upgrade_re, UPGRADE_RE_PATTERN);
    compile_regex(&cmd_any_re, CMD_ANY_RE_PATTERN);
    compile_regex(&tx_start_re, TX_START_RE_PATTERN);
    compile_regex(&hist_pkg_re, HIST_PKG_RE_PATTERN);
}

void free_all_regexes(void) {
    regfree(&pkg_re);
    regfree(&upgrade_re);
    regfree(&cmd_any_re);
    regfree(&tx_start_re);
    regfree(&hist_pkg_re);
}

static char *extract_group(const char *line, regmatch_t *matches, int idx) {
    regoff_t start = matches[idx].rm_so;
    regoff_t end = matches[idx].rm_eo;
    size_t len;
    char *out;

    if (start < 0 || end < 0)
        return NULL;

    len = (size_t)(end - start);
    out = malloc(len + 1);
    if (!out) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    memcpy(out, line + start, len);
    out[len] = '\0';
    return out;
}

static void lastseen_init(LastSeenList *ls) {
    ls->items = NULL;
    ls->count = 0;
    ls->capacity = 0;
}

static void lastseen_free(LastSeenList *ls) {
    size_t i;
    for (i = 0; i < ls->count; i++) {
        free(ls->items[i].pkg);
        free(ls->items[i].action);
    }
    free(ls->items);
    ls->items = NULL;
    ls->count = 0;
    ls->capacity = 0;
}

static void lastseen_upsert(LastSeenList *ls, const char *pkg, char *line, const char *action) {
    size_t i;
    LastSeenEntry *e;

    for (i = 0; i < ls->count; i++) {
        if (ls->items[i].active && strcmp(ls->items[i].pkg, pkg) == 0) {
            ls->items[i].active = 0;
        }
    }

    if (ls->count == ls->capacity) {
        size_t new_cap;
        LastSeenEntry *grown;

        new_cap = ls->capacity ? ls->capacity * 2 : 64;
        grown = realloc(ls->items, new_cap * sizeof(LastSeenEntry));
        if (!grown) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        ls->items = grown;
        ls->capacity = new_cap;
    }

    e = &ls->items[ls->count++];
    e->pkg = strdup(pkg);
    e->line = line;
    e->action = action ? strdup(action) : NULL;
    e->active = 1;
}

/* --installed: packages whose most recent action in the window was
 * "installed" (i.e. currently installed according to the log). */
void show_installed(int n) {
    StrList all;
    PtrList matched;
    char **window;
    size_t window_count;
    LastSeenList last_seen;
    regmatch_t m[4];
    size_t i;

    strlist_init(&all);
    iter_log_lines(&all);

    filter_lines(&all, line_is_install_or_remove, &matched);
    ptrlist_last_n(&matched, n, &window, &window_count);

    lastseen_init(&last_seen);

    for (i = 0; i < window_count; i++) {
        char *line = window[i];
        char *action;
        char *pkg;

        if (regexec(&pkg_re, line, 4, m, 0) != 0)
            continue;

        action = extract_group(line, m, 2);
        pkg = extract_group(line, m, 3);
        if (action && pkg)
            lastseen_upsert(&last_seen, pkg, line, action);

        free(action);
        free(pkg);
    }

    for (i = 0; i < last_seen.count; i++) {
        LastSeenEntry *e = &last_seen.items[i];
        if (e->active && e->action && strcmp(e->action, "installed") == 0)
            printf("%s\n", e->line);
    }

    lastseen_free(&last_seen);
    ptrlist_free(&matched);
    strlist_free(&all);
}

void show_sync_commands(int n) {
    StrList all;
    PtrList matched;
    char **window;
    size_t window_count;
    regmatch_t m[4];
    size_t i;

    strlist_init(&all);
    iter_log_lines(&all);

    filter_lines(&all, line_is_any_command, &matched);
    ptrlist_last_n(&matched, n, &window, &window_count);

    for (i = 0; i < window_count; i++) {
        char *line = window[i];
        if (regexec(&cmd_any_re, line, 4, m, 0) == 0) {
            char *cmd = extract_group(line, m, 2);
            if (cmd) {
                printf("%s\n", cmd);
                free(cmd);
                continue;
            }
        }
        printf("%s\n", line);
    }

    ptrlist_free(&matched);
    strlist_free(&all);
}

void show_upgraded(int n) {
    StrList all;
    PtrList matched;
    char **window;
    size_t window_count;
    LastSeenList last_seen;
    regmatch_t m[4];
    size_t i;

    strlist_init(&all);
    iter_log_lines(&all);

    filter_lines(&all, line_is_upgraded, &matched);
    ptrlist_last_n(&matched, n, &window, &window_count);

    lastseen_init(&last_seen);

    for (i = 0; i < window_count; i++) {
        char *line = window[i];
        char *pkg;

        if (regexec(&upgrade_re, line, 4, m, 0) != 0)
            continue;

        pkg = extract_group(line, m, 2);
        if (pkg)
            lastseen_upsert(&last_seen, pkg, line, NULL);

        free(pkg);
    }

    for (i = 0; i < last_seen.count; i++) {
        LastSeenEntry *e = &last_seen.items[i];
        if (e->active)
            printf("%s\n", e->line);
    }

    lastseen_free(&last_seen);
    ptrlist_free(&matched);
    strlist_free(&all);
}

static void histentrylist_init(HistEntryList *hl) {
    hl->items = NULL;
    hl->count = 0;
    hl->capacity = 0;
}

static void histentrylist_push(HistEntryList *hl, char *pkg, char *action, char *detail) {
    if (hl->count == hl->capacity) {
        size_t new_cap = hl->capacity ? hl->capacity * 2 : 8;
        HistEntry *grown = realloc(hl->items, new_cap * sizeof(HistEntry));
        if (!grown) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        hl->items = grown;
        hl->capacity = new_cap;
    }
    hl->items[hl->count].pkg = pkg;
    hl->items[hl->count].action = action;
    hl->items[hl->count].detail = detail;
    hl->count++;
}

static void histentrylist_free(HistEntryList *hl) {
    size_t i;
    for (i = 0; i < hl->count; i++) {
        free(hl->items[i].pkg);
        free(hl->items[i].action);
        free(hl->items[i].detail);
    }
    free(hl->items);
    hl->items = NULL;
    hl->count = 0;
    hl->capacity = 0;
}

static void txlist_init(TransactionList *tl) {
    tl->items = NULL;
    tl->count = 0;
    tl->capacity = 0;
}

static size_t txlist_push_new(TransactionList *tl, char *timestamp, char *command) {
    if (tl->count == tl->capacity) {
        size_t new_cap = tl->capacity ? tl->capacity * 2 : 16;
        Transaction *grown = realloc(tl->items, new_cap * sizeof(Transaction));
        if (!grown) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        tl->items = grown;
        tl->capacity = new_cap;
    }
    tl->items[tl->count].timestamp = timestamp;
    tl->items[tl->count].command = command;
    histentrylist_init(&tl->items[tl->count].entries);
    return tl->count++;
}

static void txlist_free(TransactionList *tl) {
    size_t i;
    for (i = 0; i < tl->count; i++) {
        free(tl->items[i].timestamp);
        free(tl->items[i].command);
        histentrylist_free(&tl->items[i].entries);
    }
    free(tl->items);
    tl->items = NULL;
    tl->count = 0;
    tl->capacity = 0;
}

static int line_is_transaction_start(const char *line) {
    return strstr(line, "[ALPM] transaction started") != NULL;
}

static int line_is_transaction_end(const char *line) {
    return strstr(line, "[ALPM] transaction completed") != NULL;
}

// n here is the number of most recent *transactions* to show, unlike
// the other modes where n counts matching log lines.
void show_history(int n) {
    StrList all;
    TransactionList tx_list;
    char *pending_cmd = NULL;
    long cur_idx = -1;
    regmatch_t m[5];
    size_t i;
    size_t start_idx, shown;

    strlist_init(&all);
    iter_log_lines(&all);

    txlist_init(&tx_list);

    for (i = 0; i < all.count; i++) {
        char *line = all.items[i];

        if (line_is_any_command(line)) {
            if (regexec(&cmd_any_re, line, 3, m, 0) == 0) {
                char *cmd = extract_group(line, m, 2);
                if (cmd) {
                    free(pending_cmd);
                    pending_cmd = cmd;
                }
            }
            continue;
        }

        if (line_is_transaction_start(line)) {
            char *ts = NULL;
            if (regexec(&tx_start_re, line, 2, m, 0) == 0)
                ts = extract_group(line, m, 1);
            cur_idx = (long)txlist_push_new(&tx_list, ts, pending_cmd);
            pending_cmd = NULL;
            continue;
        }

        if (line_is_transaction_end(line)) {
            cur_idx = -1;
            continue;
        }

        if (cur_idx >= 0 && regexec(&hist_pkg_re, line, 5, m, 0) == 0) {
            char *action = extract_group(line, m, 2);
            char *pkg = extract_group(line, m, 3);
            char *detail = extract_group(line, m, 4);
            if (action && pkg) {
                histentrylist_push(&tx_list.items[cur_idx].entries, pkg, action, detail);
            } else {
                free(pkg);
                free(action);
                free(detail);
            }
        }
    }
    free(pending_cmd);

    if (n <= 0 || (size_t)n >= tx_list.count) {
        start_idx = 0;
        shown = tx_list.count;
    } else {
        start_idx = tx_list.count - (size_t)n;
        shown = (size_t)n;
    }

    for (i = 0; i < shown; i++) {
        Transaction *t = &tx_list.items[start_idx + i];
        size_t j;

        if (t->entries.count == 0)
            continue;

        printf("%s", t->timestamp ? t->timestamp : "?");
        if (t->command)
            printf("  %s", t->command);
        printf("\n");

        for (j = 0; j < t->entries.count; j++) {
            HistEntry *e = &t->entries.items[j];
            printf("  %-10s %-30s %s\n", e->action, e->pkg, e->detail ? e->detail : "");
        }
        printf("\n");
    }

    txlist_free(&tx_list);
    strlist_free(&all);
}

void print_version(void) {
    printf("%s %s\n\n", PROJECT_NAME, PACKAGE_VERSION);
    printf("Copyright (C) 2026 Giovanni.\n");
    printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>\n");
    printf("This is free software; see the source for copying conditions.  There is NO\n");
    printf("warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\n");
    printf("Written by Giovanni.\n");
}

void print_usage_error(void) {
    fprintf(stderr, "Usage: %s {-i|--installed | -c|--show-commands | -u|--upgraded | -s|--show-hist} [n]\n", PROJECT_NAME);
    fprintf(stderr, "Try '%s --help' for more information.\n", PROJECT_NAME);
}

void print_help(void) {
    printf("Show recently installed packages, sync commands, upgrades, or\n");
    printf("transaction history from pacman.log. One mode flag is required.\n\n");
    printf("Usage: %s {-i|-c|-u|-s} [n]\n\n", PROJECT_NAME);
    printf("   n                        for -i/-c/-u: number of matching log lines to consider (default: %d)\n", DEFAULT_N);
    printf("                            for -s: number of most recent transactions to show (default: %d)\n", DEFAULT_N);
    printf("  -i, --installed           show packages currently installed according to the log\n");
    printf("  -c, --show-commands       show the last n user-initiated pacman commands\n");
    printf("  -u, --upgraded            show the last n package upgrades\n");
    printf("  -s, --show-hist           show the last n transactions, packages grouped by transaction\n");
    printf("  -h, --help                display this help and exit\n");
    printf("  -v, --version             output version information and exit\n");
}
