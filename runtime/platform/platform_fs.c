/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _WIN32
#define _POSIX_C_SOURCE   200809L /* fseeko, statvfs com -std=c17 */
#define _FILE_OFFSET_BITS 64
#endif

#include "am/platform_fs.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* UTF-8 para UTF-16; o chamador libera com free. */
static wchar_t *to_wide(const char *utf8) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (n <= 0) {
        return NULL;
    }
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (w != NULL && MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, n) <= 0) {
        free(w);
        return NULL;
    }
    return w;
}

FILE *platform_fs_fopen(const char *path, const char *mode) {
    wchar_t *wpath = to_wide(path);
    wchar_t *wmode = to_wide(mode);
    FILE *f = (wpath != NULL && wmode != NULL) ? _wfopen(wpath, wmode) : NULL;
    free(wpath);
    free(wmode);
    return f;
}

bool platform_fs_seek(FILE *file, u64 offset) {
    return _fseeki64(file, (__int64)offset, SEEK_SET) == 0;
}

bool platform_fs_size(FILE *file, u64 *size) {
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        return false;
    }
    const __int64 end = _ftelli64(file);
    if (end < 0) {
        return false;
    }
    *size = (u64)end;
    return true;
}

bool platform_fs_exists(const char *path) {
    wchar_t *w = to_wide(path);
    const bool ok = w != NULL && GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
    free(w);
    return ok;
}

int platform_fs_mkdir(const char *path) {
    wchar_t *w = to_wide(path);
    if (w == NULL) {
        return -1;
    }
    int res = -1;
    if (CreateDirectoryW(w, NULL)) {
        res = 1;
    } else if (GetLastError() == ERROR_ALREADY_EXISTS) {
        const DWORD attr = GetFileAttributesW(w);
        res = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0) ? 0 : -1;
    }
    free(w);
    return res;
}

bool platform_fs_remove(const char *path) {
    wchar_t *w = to_wide(path);
    if (w == NULL) {
        return false;
    }
    const DWORD attr = GetFileAttributesW(w);
    bool ok = false;
    if (attr != INVALID_FILE_ATTRIBUTES) {
        ok = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0 ? RemoveDirectoryW(w) != 0 : DeleteFileW(w) != 0;
    }
    free(w);
    return ok;
}

bool platform_fs_rename(const char *from, const char *to) {
    wchar_t *wf = to_wide(from);
    wchar_t *wt = to_wide(to);
    /* Sem MOVEFILE_REPLACE_EXISTING: nunca sobrescreve. */
    const bool ok = wf != NULL && wt != NULL && MoveFileExW(wf, wt, 0) != 0;
    free(wf);
    free(wt);
    return ok;
}

bool platform_fs_free_bytes(const char *path, u64 *bytes) {
    wchar_t *w = to_wide(path);
    ULARGE_INTEGER avail;
    const bool ok = w != NULL && GetDiskFreeSpaceExW(w, &avail, NULL, NULL) != 0;
    free(w);
    if (ok) {
        *bytes = (u64)avail.QuadPart;
    }
    return ok;
}

#else /* POSIX (macOS, Linux) */

#include <sys/stat.h>
#include <sys/statvfs.h>

FILE *platform_fs_fopen(const char *path, const char *mode) {
    return fopen(path, mode);
}

bool platform_fs_seek(FILE *file, u64 offset) {
    return fseeko(file, (off_t)offset, SEEK_SET) == 0;
}

bool platform_fs_size(FILE *file, u64 *size) {
    if (fseeko(file, 0, SEEK_END) != 0) {
        return false;
    }
    const off_t end = ftello(file);
    if (end < 0) {
        return false;
    }
    *size = (u64)end;
    return true;
}

bool platform_fs_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

int platform_fs_mkdir(const char *path) {
    if (mkdir(path, 0777) == 0) {
        return 1;
    }
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : -1;
}

bool platform_fs_remove(const char *path) {
    return remove(path) == 0; /* arquivo ou diretório vazio */
}

bool platform_fs_rename(const char *from, const char *to) {
    struct stat st;
    if (stat(to, &st) == 0) {
        return false; /* rename() do POSIX sobrescreveria um arquivo ou diretório vazio */
    }
    return rename(from, to) == 0;
}

bool platform_fs_free_bytes(const char *path, u64 *bytes) {
    struct statvfs sv;
    if (statvfs(path, &sv) != 0) {
        return false;
    }
    *bytes = (u64)sv.f_bavail * (u64)sv.f_frsize;
    return true;
}

#endif
