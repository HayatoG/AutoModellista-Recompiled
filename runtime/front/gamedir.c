/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "am/gamedir.h"

#include <stdio.h>
#include <string.h>

#include "am/game.h"
#include "am/platform.h"

static bool join(char *out, size_t size, const char *a, const char *b) {
    const int n = snprintf(out, size, "%s/%s", a, b);
    return n > 0 && (size_t)n < size;
}

/* A raiz da árvore: <dir> se tem sys/main.dol; o pai, se <dir> já é a sys/. */
static bool find_root(const char *dir, char *root, size_t size) {
    char path[1200];
    if (join(path, sizeof(path), dir, "sys/main.dol") && platform_path_exists(path)) {
        return snprintf(root, size, "%s", dir) > 0;
    }
    if (join(path, sizeof(path), dir, "main.dol") && platform_path_exists(path)) {
        return join(root, size, dir, "..");
    }
    return false;
}

GameDirResult gamedir_check(const char *dir, char *root_out, size_t root_size, char hash_out[SHA256_HEX_SIZE]) {
    hash_out[0] = '\0';
    char root[1024];
    char path[1200];
    if (dir == NULL || dir[0] == '\0' || !find_root(dir, root, sizeof(root))) {
        return GAMEDIR_NO_DOL;
    }
    if (!join(path, sizeof(path), root, "files") || !platform_path_exists(path)) {
        return GAMEDIR_NO_FILES;
    }
    size_t size = 0;
    u8 *dol = join(path, sizeof(path), root, "sys/main.dol") ? platform_load_file(path, &size) : NULL;
    if (dol == NULL) {
        return GAMEDIR_NO_DOL;
    }
    sha256_hex(dol, size, hash_out);
    platform_free_file(dol);
    (void)snprintf(root_out, root_size, "%s", root);
    return strcmp(hash_out, GAME_MAIN_DOL_SHA256) == 0 ? GAMEDIR_OK : GAMEDIR_BAD_HASH;
}

const char *gamedir_result_text(GameDirResult r) {
    switch (r) {
    case GAMEDIR_OK:
        return "jogo encontrado (GAUE08, revisão 0)";
    case GAMEDIR_NO_DOL:
        return "não achei sys/main.dol nessa pasta";
    case GAMEDIR_NO_FILES:
        return "falta a pasta files/ ao lado da sys/";
    case GAMEDIR_BAD_HASH:
        return "main.dol de outra versão: o executável foi recompilado do GAUE08 revisão 0";
    }
    return "?";
}
