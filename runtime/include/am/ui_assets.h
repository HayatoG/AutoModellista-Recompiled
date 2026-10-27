/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Fontes e fundo da tela inicial embutidos no executável (dados gerados por scripts/gen_launcher_assets.py a partir de
 * assets/launcher/; licenças das fontes em third_party/fonts). Ficam em runtime/platform/ui_assets_data.c. */
#ifndef AM_UI_ASSETS_H
#define AM_UI_ASSETS_H

#include "am/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum UiAssetId {
    UI_ASSET_FONT_SAIRA_400,
    UI_ASSET_FONT_SAIRA_500,
    UI_ASSET_FONT_SAIRA_600,
    UI_ASSET_FONT_SAIRA_700,
    UI_ASSET_FONT_SAIRA_COND_800, /* Saira Condensed ExtraBold: títulos */
    UI_ASSET_FONT_MONO_400,       /* JetBrains Mono: caminhos e valores */
    UI_ASSET_BG_1080,             /* PNG 1920x1080 */
    UI_ASSET_BG_2160,             /* PNG 3840x2160 */
    UI_ASSET_COUNT,
} UiAssetId;

typedef struct UiAsset {
    const unsigned char *data;
    size_t size;
} UiAsset;

UiAsset ui_asset(UiAssetId id);

#ifdef __cplusplus
}
#endif

#endif /* AM_UI_ASSETS_H */
