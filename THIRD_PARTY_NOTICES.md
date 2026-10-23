# Third-Party Notices

AQTRunCat includes material from the following third-party projects.

## RunCat Neo runner frames

- Source: https://github.com/runcat-dev/RunCatNeo
- Revision: `b3b1543049ea0a051ecb78654a45f144724ea737` (2026-08-06)
- Original location: `LocalPackage/Sources/UserInterface/Resources/Media.xcassets/Runners/`
- Copyright: Copyright 2026 Kyome22 (Takuto Nakamura)
- License: Apache License 2.0. The full text is in `third_party/runcatneo/LICENSE`.
- Files: `third_party/runcatneo/runners/{cat,coffee,engine}/*.png`
- Modifications: none. The files are byte-for-byte copies of the upstream PNGs.
- Use:
  - `cat`, `coffee` and (since 1.7.4) `engine` are compiled into the `aqtruncat` executable as bundled runners (`AQTRUNCAT_BUNDLED_RUNNERS`).
    Since F4.2 the same unmodified files are also installed as
    `share/aqtruncat/runners/<id>/*.png`, where the backend lists them for
    the KDE Plasma widget (and other frontends) to animate.
  - The M1.0 tray spike also uses `engine`.

The runner metadata files in `data/runners/<id>/metadata.json` (also for
`walkinggoose` and `partyparrot`) are AQTRunCat
files. They only describe the frames above: name, author and monochrome
rendering, taken from RunCat Neo's English runner names and its built-in
`isTemplate = true`.

The RunCat and RunCat Neo names, application icon and branding are not part
of this grant (Apache-2.0 §6) and are not used as AQTRunCat branding.

## Party Parrot runner frames

- Source: the "Party Parrot" internet meme (GIF with sunglasses,
  `partyparrt-30.gif`, downloaded from Tenor).
- Original author: not verifiable.
- License: unknown. Included by the explicit decision of the AQTRunCat
  maintainer (2026-10-22); no license is claimed for these files.
- Files: `third_party/partyparrot/runners/parrot/*.png`.
- Modifications: cropped, decoloured to a monochrome outline and scaled to
  50×36 (details in `third_party/partyparrot/README.md`).
- Use: built-in runner `partyparrot` ("Party Parrot", since 1.7.3),
  compiled into `aqtruncat` and installed as
  `share/aqtruncat/runners/partyparrot/`.

## Walking Goose runner frames

- Source: GIF of a walking cartoon goose (`funny-as-hell.gif`, downloaded
  from Tenor).
- Original author: not verifiable.
- License: unknown. Included by the explicit decision of the AQTRunCat
  maintainer (2026-10-22); no license is claimed for these files.
- Files: `third_party/walkinggoose/runners/goose/*.png`.
- Modifications: shadow removed, decoloured to a monochrome outline and
  scaled to 24×36 (details in `third_party/walkinggoose/README.md`).
- Use: built-in runner `walkinggoose` ("Walking Goose", since 1.7.3),
  compiled into `aqtruncat` and installed as
  `share/aqtruncat/runners/walkinggoose/`.

## Rickroll runner frames

- Subject: Rick Astley in the music video of "Never Gonna Give You Up"
  (1987), as used by the "Rickroll" internet meme.
- Source: a GIF excerpt of the video circulating as the Rickroll meme.
- License: none claimed. Included by the explicit decision of the AQTRunCat
  maintainer (2026-10-23).
- Files: `third_party/rickroll/runners/rickroll/*.png`.
- Modifications: figure separated from the background, face and shirt cut
  out as a monochrome stencil, scaled to 33×36 (details in
  `third_party/rickroll/README.md`).
- Use: built-in runner `rickroll` ("Rickroll", since 2.1.1), compiled
  into `aqtruncat` and installed as `share/aqtruncat/runners/rickroll/`.
