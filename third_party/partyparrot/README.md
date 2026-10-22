# Party Parrot runner frames

Frames of the built-in runner `partyparrot` ("Party Parrot", AQTRunCat
F7.2, since 1.7.3). Built into `aqtruncat` and installed as
`share/aqtruncat/runners/partyparrot/` (the source folder name `parrot` is
not the runner id).

- Source: the "Party Parrot" internet meme, GIF with sunglasses
  `partyparrt-30.gif` downloaded from Tenor (320×229, 10 frames, 0.04 s
  each, transparent background; SHA-256
  `c8c50162cc56940b8441a4a23103044128fd9f02a31cc7175e405fbbb3bddfed`).
- Original author: not verifiable (the GIF has been re-shared many times).
- License: unknown. Included by the explicit decision of the AQTRunCat
  maintainer (2026-10-22); no license is claimed for these files.
- Modifications (AQTRunCat): all frames cropped with one common box
  (311×223+9+6 of the coalesced frames), decoloured to a monochrome
  "outline" (only the dark line art kept: luminance below 90/255 inside
  alpha ≥ 50 %, thickened with a 9×9 square), scaled to 50×36 with a
  Lanczos filter, stored as black pixels with the line art in the alpha
  channel (`monochrome` runner: the tray tints it with the panel text
  colour).
- Files: `runners/parrot/parrot-frame-{0..9}.png`.
