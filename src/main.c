/* main.c
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

#include "vlp.h"

int main(int argc, char **argv) {
    int argi = 1;
    int show_cmds = 0;
    int show_upg = 0;
    int show_inst = 0;
    int show_hist = 0;
    int n = DEFAULT_N;

    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_help();
        return 0;
    }

    if (argc > 1 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)) {
        print_version();
        return 0;
    }

    if (argi < argc && (strcmp(argv[argi], "--installed") == 0 || strcmp(argv[argi], "-i") == 0)) {
        show_inst = 1;
        argi++;
    } else if (argi < argc && (strcmp(argv[argi], "--show-commands") == 0 || strcmp(argv[argi], "-c") == 0)) {
        show_cmds = 1;
        argi++;
    } else if (argi < argc && (strcmp(argv[argi], "--upgraded") == 0 || strcmp(argv[argi], "-u") == 0)) {
        show_upg = 1;
        argi++;
    } else if (argi < argc && (strcmp(argv[argi], "--show-hist") == 0 || strcmp(argv[argi], "-s") == 0)) {
        show_hist = 1;
        argi++;
    }

    if (!show_inst && !show_cmds && !show_upg && !show_hist) {
        print_usage_error();
        return 1;
    }

    if (argi < argc) {
        char *endptr = NULL;
        long parsed = strtol(argv[argi], &endptr, 10);
        if (endptr == argv[argi] || *endptr != '\0') {
            print_usage_error();
            return 1;
        }
        n = (int)parsed;
    }

    compile_all_regexes();

    if (show_cmds) {
        show_sync_commands(n);
    } else if (show_upg) {
        show_upgraded(n);
    } else if (show_inst) {
        show_installed(n);
    } else if (show_hist) {
        show_history(n);
    }

    free_all_regexes();
    return 0;
}
