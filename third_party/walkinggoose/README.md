# Walking Goose runner frames

Frames of the built-in runner `walkinggoose` ("Walking Goose", AQTRunCat
F7.1, since 1.7.3). Built into `aqtruncat` and installed as
`share/aqtruncat/runners/walkinggoose/` (the source folder name `goose` is
not the runner id).

- Source: GIF of a walking cartoon goose, `funny-as-hell.gif`, downloaded
  from Tenor (220×220, 6 frames, 0.10 s each, opaque white background, white
  body, grey shadow; SHA-256
  `95b260c78bd420cb4471ab39f2b1667a0e7750274dd240730dca2050cbb07c5f`).
- Original author: not verifiable (the GIF has been re-shared many times).
- License: unknown. Included by the explicit decision of the AQTRunCat
  maintainer (2026-10-22); no license is claimed for these files.
- Modifications (AQTRunCat), monochrome "outline" like the parrot:
  - line art = luminance below 90/255 (black lines and their dark brown
    edges); the white background and the white body drop out by
    themselves;
  - shadow removed: the dark brown also fills the shadow, so its thick
    parts are found with an opening (9×9 erosion, 13×13 dilation) and
    subtracted; pure black (luminance below 10) is always kept;
  - beak and feet (orange, R − B > 100) kept solid;
  - connected bits smaller than 150 px whose centre lies below y = 160
    (the shadow band) removed; the eye stays;
  - one common crop box for all frames (the union of the line art),
    lines thickened with a 5×5 square, scaled to 24×36 with a Lanczos
    filter, stored as black pixels with the drawing in the alpha channel
    (`monochrome` runner: the tray tints it with the panel text colour).
- Files: `runners/goose/goose-frame-{0..5}.png`.
