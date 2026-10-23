#!/usr/bin/env python3
# Rebuilds runners/rickroll/rickroll-frame-*.png from the source GIF (see
# README.md for its SHA-256). Maintainer tool, not used by the build.
# Needs ImageMagick (`magick`), Pillow and NumPy.
# Usage: make-frames.py SOURCE.gif OUTPUT_DIR
import glob
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image, ImageDraw, ImageFilter


def silhouette(rgb):
    # The figure against the light, bluish background: dark or reddish
    # pixels, specks removed, parts joined, holes filled from the border.
    a = rgb.astype(int)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    lum = 0.299 * r + 0.587 * g + 0.114 * b
    m = Image.fromarray((((lum < 120) | ((r - b) > 20)) * 255).astype('uint8'))
    m = m.filter(ImageFilter.MinFilter(5)).filter(ImageFilter.MaxFilter(5))
    m = m.filter(ImageFilter.MaxFilter(7)).filter(ImageFilter.MinFilter(7))
    w, h = m.size
    pad = Image.new('L', (w + 2, h + 2), 0)
    pad.paste(m, (1, 1))
    ImageDraw.floodfill(pad, (0, 0), 128)
    return np.asarray(pad)[1:-1, 1:-1] != 128


def stencil(rgb, figure):
    # Light areas inside the figure (face, collar, shirt) cut out.
    a = rgb.astype(int)
    lum = 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]
    light = Image.fromarray(((lum > 150) * 255).astype('uint8'))
    light = light.filter(ImageFilter.MinFilter(9)).filter(ImageFilter.MaxFilter(9))
    return figure & ~(np.asarray(light) > 0)


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: make-frames.py SOURCE.gif OUTPUT_DIR')
    source, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(['magick', source, '-coalesce', os.path.join(tmp, 'f-%02d.png')], check=True)
        frames = [np.asarray(Image.open(p).convert('RGB')) for p in sorted(glob.glob(os.path.join(tmp, 'f-*.png')))]
    figures = [silhouette(f) for f in frames]
    # One crop box for all frames, from the union of the figures.
    ys, xs = np.nonzero(np.any(np.stack(figures), axis=0))
    box = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
    kept = [i for i in range(len(frames)) if i == 0 or not np.array_equal(frames[i], frames[i - 1])]
    for n, i in enumerate(kept):
        im = Image.fromarray((stencil(frames[i], figures[i]) * 255).astype('uint8')).crop(box)
        h = 36
        w = round(im.width * h / im.height)
        rgba = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        rgba.putalpha(im.resize((w, h), Image.LANCZOS))
        rgba.save(os.path.join(out, f'rickroll-frame-{n}.png'))
    print(f'{len(frames)} frames, {len(frames) - len(kept)} duplicates dropped, '
          f'box {box[2] - box[0]}x{box[3] - box[1]}+{box[0]}+{box[1]}, {len(kept)} frames written to {out}')


if __name__ == '__main__':
    main()
