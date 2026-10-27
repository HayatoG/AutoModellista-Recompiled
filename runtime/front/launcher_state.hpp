/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Estado da tela inicial, compartilhado entre launcher.cpp (fluxo, Início, menu lateral) e launcher_pages.cpp (as
 * páginas de configuração). Só launcher*.cpp inclui. */
#ifndef AM_LAUNCHER_STATE_HPP
#define AM_LAUNCHER_STATE_HPP

#include "am/banner.h"
#include "am/front.h"
#include "am/gamedir.h"

#include "launcher_ui.hpp"

namespace launcher {

enum class Page { Home, Graphics, Audio, Controls, Interface, Support, About };
constexpr int kPageCount = 7;

/* Mudança que só pode ser aplicada fora do quadro do ImGui (troca a forma de apresentação). */
enum class Pending { None, Backend };

struct State {
    Settings *s;
    const LauncherInfo *info;
    Page page;
    bool done;
    LauncherResult result;
    Pending pending;
    Banner banner;
    bool have_banner;
    u32 art;
    char loaded_art_dir[SETTINGS_PATH_SIZE];
    GameDirResult dir_state;
    char dir_hash[SHA256_HEX_SIZE];
    char typed[SETTINGS_PATH_SIZE]; /* a pasta que não conferiu, para mostrar */
    bool dialog_folder;
    bool dialog_card;
    int capture;        /* controle sendo remapeado; -1 = nenhum */
    bool focus_nav;     /* o próximo quadro foca o item do menu lateral da página atual */
    bool focus_content; /* o próximo quadro foca o primeiro controle da página */
    char message[256];
    u64 copied_until_ns; /* "Copiado" no botão do Suporte */
    /* fundo e vidro */
    u32 bg_handle;
    u32 bg_kind; /* 0 = 1920x1080, 1 = 3840x2160 */
    bool bg_loaded;
    u8 *mid; /* o fundo reduzido a 480x270, de onde sai a miniatura do vidro */
    u32 thumb_handle;
    u32 thumb_blur;
    /* captura de desenvolvimento (AM_LAUNCHER_SHOT) */
    bool shot_mode;
};

extern State g;

constexpr u32 kMidW = 480;
constexpr u32 kMidH = 270;

void go(Page page);
void check_dir(const char *dir);
bool back_pressed();

/* launcher_pages.cpp: o conteúdo de uma página de configuração dentro do painel `a`..`b` (cabeçalho e rolagem). */
void draw_page(Page page, ImVec2 a, ImVec2 b);
/* O texto de "Informações do sistema" (também copiado pelo botão). */
void system_info(char *out, size_t size);
const char *driver_label(char *out, size_t size);

} // namespace launcher

#endif /* AM_LAUNCHER_STATE_HPP */
