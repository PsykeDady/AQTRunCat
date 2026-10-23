# Rickroll runner frames

Frames of the built-in runner `rickroll` ("Rickroll", AQTRunCat v2F1,
since 2.1.1). Prepared and approved on 2026-10-23. Built into `aqtruncat`
and installed as `share/aqtruncat/runners/rickroll/`.

- Subject: Rick Astley in the music video of "Never Gonna Give You Up"
  (1987), as used by the "Rickroll" internet meme.
- Source: a GIF excerpt of the video circulating as the Rickroll meme
  (`rickroll.gif`, 498×427, 32 frames, 0.05 s each, opaque background;
  SHA-256
  `9218939cc5f85280d7431fa015054cb2e108b09c347526f03ccfbcb358962d36`).
- License: none claimed. Included by the explicit decision of the AQTRunCat
  maintainer (2026-10-23).
- Modifications (AQTRunCat): frame 1 dropped (identical to frame 0); the
  figure separated from the light background (foreground: luminance below
  120/255 or red minus blue above 20, opened with a 5×5 and closed with a
  7×7 square, holes filled by flooding the background from the border);
  light areas inside the figure (luminance above 150/255, opened with a
  9×9 square: face, collar, shirt) cut out as a stencil; all frames cropped
  with one common box (379×412+82+15), scaled to 33×36 with a Lanczos
  filter, stored as black pixels with the figure in the alpha channel
  (`monochrome` runner: the tray tints it with the panel text colour).
- Files: `runners/rickroll/rickroll-frame-{0..30}.png`.
- Reproducible: `make-frames.py SOURCE.gif OUTPUT_DIR` (AQTRunCat's own
  maintainer script, Apache-2.0; ImageMagick, Pillow, NumPy) rebuilds the
  31 frames byte for byte from the GIF above (verified 2026-10-23). The
  GIF itself is not in the repository.
