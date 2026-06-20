#!/usr/bin/env python3
"""Gera proceduralmente as texturas usadas no projeto final.

Cria de forma binrio lua e grama:

  - assets/tex/grass.png  (64x64)  : grama verde com ruído + faixa de terra
  - assets/tex/moon.png   (256x256): superfície lunar cinza com crateras

Requer Pillow:
    pip install pillow

Uso (a partir da raiz do repositório):
    python3 tools/gen_textures.py
"""

import os
import random

from PIL import Image

# Diretório de saída relativo à raiz do repositório (este script vive em tools/).
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEX_DIR = os.path.join(REPO_ROOT, "assets", "tex")

SEED = 1337


def _lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def gen_grass(size=64):
    """Grama verde (parte de cima) sobre uma faixa de terra (parte de baixo).

    Cores amostradas da textura original:
        verdes : (54,110,38) -> (74,142,50) -> (104,176,70)
        terra  : (110,78,48)
    """
    rng = random.Random(SEED)
    img = Image.new("RGB", (size, size))
    px = img.load()

    greens = [(54, 110, 38), (74, 142, 50), (104, 176, 70)]
    dirt = (110, 78, 48)
    dirt_dark = (84, 58, 34)

    dirt_start = int(size * 0.84)  # ~16% inferior vira terra

    for y in range(size):
        for x in range(size):
            if y < dirt_start:
                # ruído de grama: escolhe um tom de verde por pixel
                base = rng.choice(greens)
                jitter = rng.randint(-10, 10)
                px[x, y] = tuple(max(0, min(255, c + jitter)) for c in base)
            else:
                base = dirt if rng.random() > 0.30 else dirt_dark
                jitter = rng.randint(-8, 8)
                px[x, y] = tuple(max(0, min(255, c + jitter)) for c in base)

    # alguns fios de grama descendo sobre a terra
    for x in range(size):
        if rng.random() < 0.55:
            blade = rng.randint(1, 3)
            for dy in range(blade):
                yy = dirt_start + dy
                if yy < size:
                    px[x, yy] = rng.choice(greens)

    return img


def _draw_crater(px, size, cx, cy, r, rng):
    """Cratera: disco escuro com uma borda (anel) mais clara."""
    rim = (210, 210, 202)
    floor = (132, 132, 126)
    for y in range(max(0, cy - r - 1), min(size, cy + r + 2)):
        for x in range(max(0, cx - r - 1), min(size, cx + r + 2)):
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            if d <= r - 1.5:
                j = rng.randint(-6, 6)
                px[x, y] = tuple(max(0, min(255, c + j)) for c in floor)
            elif d <=r + 0.5:
                px[x, y] = rim


def gen_moon(size=256):
    """Superfície lunar: base cinza ruidosa + crateras de vários tamanhos.

    """
    rng = random.Random(SEED + 1)
    img = Image.new("RGB", (size, size))
    px = img.load()

    # base ruidosa (cinza levemente quente: R=G > B)
    for y in range(size):
        for x in range(size):
            v = rng.randint(150, 205)
            px[x, y] = (v, v, max(0, v - 8))

    # "mares" — manchas grandes e suaves um pouco mais escuras
    for _ in range(4):
        cx, cy = rng.randint(0, size), rng.randint(0, size)
        r = rng.randint(size // 6, size // 4)
        for y in range(max(0, cy - r), min(size, cy + r)):
            for x in range(max(0, cx - r), min(size, cx + r)):
                d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
                if d <= r:
                    t = 1.0 - d / r
                    px[x, y] = _lerp(px[x, y], (150, 150, 144), 0.35 * t)

    # crateras pequenas e médias
    n_craters = 38
    for _ in range(n_craters):
        r = rng.randint(3, 16)
        cx = rng.randint(r, size - r)
        cy = rng.randint(r, size - r)
        _draw_crater(px, size, cx, cy, r, rng)

    return img


def main():
    os.makedirs(TEX_DIR, exist_ok=True)

    grass_path = os.path.join(TEX_DIR, "grass.png")
    moon_path = os.path.join(TEX_DIR, "moon.png")

    gen_grass().save(grass_path)
    print(f"escrito: {grass_path}")

    gen_moon().save(moon_path)
    print(f"escrito: {moon_path}")


if __name__ == "__main__":
    main()
