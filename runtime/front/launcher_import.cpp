/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Escolher a imagem do jogo na tela inicial (ISO, GCM ou RVZ): extrai com disc_extract_ex (am/disc_extract.h) numa
 * thread própria para <pasta de dados>/jogo, sem nunca sobrescrever uma extração que já existe, e ao terminar põe a
 * pasta em game_dir e confere o SHA-256 do main.dol pela verificação de sempre (gamedir_check). A interface só lê o
 * progresso (barra, arquivo atual) e pode cancelar; sair da tela espera a thread terminar. */
#include <cstdio>
#include <cstring>

#include "am/gamedir.h"
#include "am/platform.h"

#include "launcher_state.hpp"

extern "C" {
#include "am/log.h" /* sem guarda extern "C" no header */
}

namespace launcher {

namespace {

bool progress_cb(const DiscExtractProgress *p, void *user) {
    Import *im = static_cast<Import *>(user);
    platform_lock(im->lock);
    im->bytes_done = p->bytes_done;
    im->bytes_total = p->bytes_total;
    im->files_done = p->files_done;
    im->files_total = p->files_total;
    (void)std::snprintf(im->current, sizeof(im->current), "%s", p->current != nullptr ? p->current : "");
    platform_unlock(im->lock);
    return im->cancel == 0;
}

int thread_main(void *user) {
    Import *im = static_cast<Import *>(user);
    DiscExtractOptions opt;
    std::memset(&opt, 0, sizeof(opt));
    opt.progress = progress_cb;
    opt.user = im;
    opt.cancel = &im->cancel;
    opt.detail = im->detail;
    opt.detail_size = sizeof(im->detail);
    const DiscExtractResult r = disc_extract_ex(im->image, im->out, &opt);
    platform_lock(im->lock);
    im->result = r;
    im->finished = true;
    platform_unlock(im->lock);
    return 0;
}

/* Põe a árvore extraída em game_dir e confere o SHA-256 (a mesma verificação do botão Escolher pasta). */
void adopt(const char *dir) {
    check_dir(dir);
    g.typed[0] = '\0';
    if (g.dir_state == GAMEDIR_OK) {
        g.message[0] = '\0';
    } else {
        (void)std::snprintf(g.typed, sizeof(g.typed), "%s", dir);
        (void)std::snprintf(g.message, sizeof(g.message), "A pasta extraída não confere: %s.",
                            gamedir_result_text(g.dir_state));
    }
}

void finish(DiscExtractResult r) {
    Import &im = g.imp;
    if (im.detail[0] != '\0') {
        AM_LOG_WARN("extração: %s", im.detail);
    }
    if (r == DISC_EXTRACT_OK) {
        adopt(im.out);
    } else if (r == DISC_EXTRACT_ERR_OTHER_GAME) {
        (void)std::snprintf(g.message, sizeof(g.message),
                            "Outro jogo ou outra versão (main.dol diferente): a pasta do jogo não foi trocada.");
    } else {
        (void)std::snprintf(g.message, sizeof(g.message), "%s", disc_extract_result_text(r));
    }
}

} // namespace

bool importing() {
    return g.imp.phase != ImportPhase::Idle;
}

void import_start(const char *image) {
    Import &im = g.imp;
    if (im.phase != ImportPhase::Idle || image == nullptr || image[0] == '\0') {
        return;
    }
    if (im.out_override[0] != '\0') {
        (void)std::snprintf(im.out, sizeof(im.out), "%s", im.out_override);
    } else {
        char dir[SETTINGS_PATH_SIZE];
        if (!platform_config_dir(dir, sizeof(dir))) {
            (void)std::snprintf(g.message, sizeof(g.message), "Sem pasta de dados para extrair a imagem.");
            return;
        }
        (void)std::snprintf(im.out, sizeof(im.out), "%sjogo", dir);
    }
    (void)std::snprintf(im.image, sizeof(im.image), "%s", image);
    g.message[0] = '\0';
    char root[SETTINGS_PATH_SIZE];
    char hash[SHA256_HEX_SIZE];
    if (gamedir_check(im.out, root, sizeof(root), hash) == GAMEDIR_OK) {
        im.phase = ImportPhase::Offer; /* já existe: nunca sobrescreve, oferece usar */
        return;
    }
    if (im.lock == nullptr) {
        im.lock = platform_lock_create();
    }
    im.bytes_done = im.bytes_total = 0;
    im.files_done = im.files_total = 0;
    im.current[0] = '\0';
    im.detail[0] = '\0';
    im.finished = false;
    __atomic_store_n(&im.cancel, 0, __ATOMIC_SEQ_CST);
    im.thread = im.lock != nullptr ? platform_thread_start("extrair imagem", thread_main, &im) : nullptr;
    if (im.thread == nullptr) {
        (void)std::snprintf(g.message, sizeof(g.message), "Não consegui iniciar a extração.");
        return;
    }
    im.phase = ImportPhase::Running;
}

void import_use_existing() {
    Import &im = g.imp;
    if (im.phase == ImportPhase::Offer) {
        im.phase = ImportPhase::Idle;
        adopt(im.out);
    }
}

void import_cancel() {
    Import &im = g.imp;
    if (im.phase == ImportPhase::Offer || im.fake) {
        im.phase = ImportPhase::Idle;
        im.fake = false;
    } else if (im.phase == ImportPhase::Running) {
        __atomic_store_n(&im.cancel, 1, __ATOMIC_SEQ_CST); /* a thread para na próxima 1 MiB; import_poll junta */
    }
}

void import_poll() {
    Import &im = g.imp;
    if (im.phase != ImportPhase::Running || im.fake) {
        return;
    }
    platform_lock(im.lock);
    const bool done = im.finished;
    const DiscExtractResult r = im.result;
    platform_unlock(im.lock);
    if (!done) {
        return;
    }
    platform_thread_join(im.thread);
    im.thread = nullptr;
    im.phase = ImportPhase::Idle;
    finish(r);
}

void import_shutdown() {
    Import &im = g.imp;
    if (im.thread != nullptr) {
        __atomic_store_n(&im.cancel, 1, __ATOMIC_SEQ_CST);
        platform_thread_join(im.thread); /* o extrator apaga o que criou ao cancelar */
        im.thread = nullptr;
    }
    im.phase = ImportPhase::Idle;
}

} // namespace launcher
