#!/usr/bin/env python3
"""Genere src/police.h a partir de la police Aileron (CC0) fournie avec Pillow >= 10.1.

Usage : gen_police.py   (depuis pendant/stm32/haptique, avec Pillow installe)
Chaque pixel de glyphe est une opacite de 0 a 15, sur un octet.
"""
from PIL import Image, ImageDraw, ImageFont

TEXTE = "".join(chr(c) for c in range(32, 127)) + "\xE9\xE8\xEA\xE0"
# Cette police n'a pas d'accents : ils sont traces au-dessus de la lettre de base.
ACCENTS = {"\xE9": ("e", ((0.35, 1.0), (0.65, 0.0))), "\xE8": ("e", ((0.35, 0.0), (0.65, 1.0))),
           "\xEA": ("e", ((0.2, 1.0), (0.5, 0.0), (0.8, 1.0))), "\xE0": ("a", ((0.35, 0.0), (0.65, 1.0)))}
POLICES = [("P18", 18, TEXTE), ("P30", 30, TEXTE), ("P34", 34, "0123456789"), ("P72", 72, "XYZ")]

sortie = ["/* Genere par gen_police.py, ne pas modifier a la main. */", "#pragma once", "#include <stdint.h>", "",
          "typedef struct { uint8_t l, h; int8_t x, y; uint8_t av; uint32_t pos; } Glyphe;",
          "typedef struct { const char *codes; const Glyphe *g; const uint8_t *d; uint8_t hauteur; } Police;", ""]
for nom, taille, chars in POLICES:
    font = ImageFont.load_default(taille)
    asc, desc = font.getmetrics()
    donnees, glyphes = [], []
    for c in chars:
        im = Image.new("L", (taille * 2, taille * 2), 0)
        base, trait = ACCENTS.get(c, (c, None))
        ImageDraw.Draw(im).text((taille // 2, asc), base, fill=255, font=font, anchor="ls")
        if trait:
            x0, y0, x1, _ = font.getbbox(base, anchor="ls")
            e = 4
            calque = Image.new("L", (im.width * e, im.height * e), 0)
            haut = asc + y0 - 0.27 * taille
            points = [(e * (taille // 2 + x0 + px * (x1 - x0)), e * (haut + py * 0.2 * taille)) for px, py in trait]
            ImageDraw.Draw(calque).line(points, fill=255, width=max(1, round(e * taille * 0.08)))
            im = Image.composite(Image.new("L", im.size, 255), im, calque.resize(im.size, Image.LANCZOS))
        b = im.getbbox() or (0, 0, 0, 0)
        l, h = b[2] - b[0], b[3] - b[1]
        pos = len(donnees)
        for y in range(b[1], b[3]):
            donnees += [(im.getpixel((x, y)) + 8) // 17 for x in range(b[0], b[2])]
        glyphes.append((l, h, b[0] - taille // 2, b[1], round(font.getlength(c)), pos))
    # Une chaine par caractere : un echappement \x ne deborde pas sur le suivant.
    codes = " ".join(f'"\\x{ord(c):02X}"' for c in chars)
    sortie.append(f"static const uint8_t {nom}_D[] = {{")
    for i in range(0, len(donnees), 40):
        sortie.append(",".join(map(str, donnees[i:i + 40])) + ",")
    sortie.append("};")
    sortie.append(f"static const Glyphe {nom}_G[] = {{")
    sortie += ["{%d,%d,%d,%d,%d,%d}," % g for g in glyphes]
    sortie.append("};")
    sortie.append(f"static const Police {nom} = {{ {codes}, {nom}_G, {nom}_D, {asc + desc} }};")
    sortie.append("")
open("src/police.h", "w", newline="\n").write("\n".join(sortie))
