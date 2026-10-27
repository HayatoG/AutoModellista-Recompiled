/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Extrator de linha de comando: imagem ISO/GCM/RVZ do GameCube -> sys/ + files/ (o mesmo código que o launcher usa).
 *
 *   disc_extract <imagem> <pasta-de-destino> [--keep-other-game]
 *
 * Compila sozinho (sem SDL e sem CMake), a partir da raiz do repositório:
 *   clang -std=c17 -O2 -Iruntime/include -Ithird_party/zstd tools/disc_extract/main.c runtime/core/disc_extract.c \
 *     runtime/core/disc_image.c runtime/core/disc_rvz.c runtime/core/sha256.c runtime/platform/platform_fs.c \
 *     third_party/zstd/zstddeclib.c -o disc_extract
 * Código de saída: 0 ok; senão o número do DiscExtractResult (a mensagem vai para a saída de erro). */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "am/disc_extract.h"

static bool show_progress(const DiscExtractProgress *p, void *user) {
    (void)user;
    const double mb = (double)p->bytes_done / (1024.0 * 1024.0);
    const double total = (double)p->bytes_total / (1024.0 * 1024.0);
    (void)fprintf(stderr, "\r%6.1f / %6.1f MiB  %u/%u  %-48.48s", mb, total, p->files_done, p->files_total, p->current);
    return true;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        (void)fprintf(stderr, "uso: %s <imagem.iso|gcm|rvz> <pasta-de-destino> [--keep-other-game]\n", argv[0]);
        return 64;
    }
    char detail[256];
    DiscExtractOptions options;
    memset(&options, 0, sizeof(options));
    options.progress = show_progress;
    options.detail = detail;
    options.detail_size = sizeof(detail);
    options.keep_other_game = argc > 3 && strcmp(argv[3], "--keep-other-game") == 0;
    const clock_t start = clock();
    const DiscExtractResult res = disc_extract_ex(argv[1], argv[2], &options);
    (void)fprintf(stderr, "\n%s\n", disc_extract_result_text(res));
    if (detail[0] != '\0') {
        (void)fprintf(stderr, "detalhe: %s\n", detail);
    }
    (void)fprintf(stderr, "tempo de CPU: %.1f s\n", (double)(clock() - start) / (double)CLOCKS_PER_SEC);
    return (int)res;
}
