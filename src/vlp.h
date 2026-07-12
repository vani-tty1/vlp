#pragma once

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <zlib.h>

#define LOG_PATH            "/var/log/pacman.log"
#define LOG_PATH_ROTATED    "/var/log/pacman.log.1"
#define LOG_PATH_ROTATED_GZ "/var/log/pacman.log.1.gz"
#define DEFAULT_N 20

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} StrList;

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} PtrList;

typedef struct {
    char *pkg;
    char *line;
    char *action;
    int active;
} LastSeenEntry;

typedef struct {
    LastSeenEntry *items;
    size_t count;
    size_t capacity;
} LastSeenList;

typedef struct {
    char *pkg;
    char *action;
    char *detail;
} HistEntry;

typedef struct {
    HistEntry *items;
    size_t count;
    size_t capacity;
} HistEntryList;

typedef struct {
    char *timestamp;
    char *command;
    HistEntryList entries;
} Transaction;

typedef struct {
    Transaction *items;
    size_t count;
    size_t capacity;
} TransactionList;

typedef int (*line_pred)(const char *line);


void compile_all_regexes(void);
void free_all_regexes(void);
void show_installed(int n);
void show_sync_commands(int n);
void show_upgraded(int n);
void show_history(int n);
void print_version(void);
void print_help(void);
void print_usage_error(void);