#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Gera runtime/platform/ui_assets_data.c (fontes e fundo da tela inicial embutidos) a partir de assets/launcher/.

    python3 -I scripts/gen_launcher_assets.py                     # regenera o .c
    python3 -I scripts/gen_launcher_assets.py --make-fonts DIR    # refaz os TTF de assets/launcher/ (precisa de fontTools)

--make-fonts espera em DIR os arquivos de github.com/google/fonts (ofl/): "Saira[wdth,wght].ttf",
"SairaCondensed-ExtraBold.ttf" e "JetBrainsMono[wght].ttf". Saira e JetBrains Mono viram instâncias estáticas com
subconjunto Latin (ASCII, Latin-1, Latin Extended-A, pontuação e setas); a Saira Condensed (nome "Saira" reservado
pela licença) é copiada sem alteração. Detalhes em third_party/fonts/README.md.
"""
import os
import shutil
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
ASSETS = os.path.join(ROOT, "assets", "launcher")
OUT = os.path.join(ROOT, "runtime", "platform", "ui_assets_data.c")

# (enumerador de am/ui_assets.h, arquivo em assets/launcher/)
TABLE = [
    ("UI_ASSET_FONT_SAIRA_400", "Saira-400.ttf"),
    ("UI_ASSET_FONT_SAIRA_500", "Saira-500.ttf"),
    ("UI_ASSET_FONT_SAIRA_600", "Saira-600.ttf"),
    ("UI_ASSET_FONT_SAIRA_700", "Saira-700.ttf"),
    ("UI_ASSET_FONT_SAIRA_COND_800", "SairaCondensed-ExtraBold.ttf"),
    ("UI_ASSET_FONT_MONO_400", "JetBrainsMono-400.ttf"),
    ("UI_ASSET_BG_1080", "background_1920x1080.png"),
    ("UI_ASSET_BG_2160", "background_3840x2160.png"),
]

UNICODES = "U+0020-007E,U+00A0-00FF,U+0100-017F,U+2010-2015,U+2018-201E,U+2022,U+2026,U+2190-2193,U+2212,U+20AC"


def make_fonts(src):
    from fontTools import subset
    from fontTools.ttLib import TTFont
    from fontTools.varLib import instancer

    def cut(font, name):
        tmp = os.path.join(ASSETS, name + ".full")
        font.save(tmp)
        opts = subset.Options()
        opts.layout_features = ["kern", "liga"]
        sub = subset.load_font(tmp, opts)
        s = subset.Subsetter(opts)
        s.populate(unicodes=subset.parse_unicodes(UNICODES))
        s.subset(sub)
        subset.save_font(sub, os.path.join(ASSETS, name), opts)
        os.remove(tmp)

    for w in (400, 500, 600, 700):
        f = instancer.instantiateVariableFont(TTFont(os.path.join(src, "Saira[wdth,wght].ttf")), {"wght": w, "wdth": 100})
        cut(f, "Saira-%d.ttf" % w)
    f = instancer.instantiateVariableFont(TTFont(os.path.join(src, "JetBrainsMono[wght].ttf")), {"wght": 400})
    cut(f, "JetBrainsMono-400.ttf")
    shutil.copy(os.path.join(src, "SairaCondensed-ExtraBold.ttf"), os.path.join(ASSETS, "SairaCondensed-ExtraBold.ttf"))


def array(name, data):
    lines = []
    for i in range(0, len(data), 40):
        lines.append(",".join(str(b) for b in data[i : i + 40]))
    return "static const unsigned char %s[%d] = {\n%s\n};\n" % (name, len(data), ",\n".join(lines))


def generate():
    out = [
        "/* SPDX-License-Identifier: GPL-3.0-or-later */\n"
        "/* GERADO por scripts/gen_launcher_assets.py a partir de assets/launcher/: não editar. Fontes OFL (third_party/fonts)\n"
        " * e o fundo da tela inicial. */\n"
        "/* clang-format off */\n"
        '#include "am/ui_assets.h"\n\n'
    ]
    for enum, fname in TABLE:
        with open(os.path.join(ASSETS, fname), "rb") as f:
            out.append(array("k_" + enum.lower(), f.read()))
    out.append("\nstatic const UiAsset k_assets[UI_ASSET_COUNT] = {\n")
    for enum, fname in TABLE:
        out.append("    [%s] = {k_%s, sizeof(k_%s)},\n" % (enum, enum.lower(), enum.lower()))
    out.append("};\n\nUiAsset ui_asset(UiAssetId id) {\n    return k_assets[id];\n}\n")
    with open(OUT, "w", newline="\n", encoding="utf-8") as f:
        f.write("".join(out))
    print("escrito", OUT)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--make-fonts":
        make_fonts(sys.argv[2])
    generate()
