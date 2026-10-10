#pragma once

#include "runner.h"

#include <optional>

namespace aqtruncat {

// Upper bounds that protect against absurd or hostile input (e.g. a "PNG"
// declaring 50000×50000 pixels). Far above anything a tray runner needs.
inline constexpr int RunnerMaxFrameSide = 1024;
inline constexpr int RunnerMaxFrames = 256;

struct RunnerLoadResult {
    std::optional<Runner> runner; // set when the runner is usable
    QString error;                // why it is not usable (fatal)

    bool isValid() const { return runner.has_value(); }
};

// Loads and validates the runner stored in `directory`.
//
// Layout (Runner Gallery / RunCat Neo compatible):
//   <directory>/<name>-frame-0.png …          frames directly in the directory, or
//   <directory>/<name>-frames/<name>-frame-0.png …  one sub-directory holding the
//                                              frames (a Gallery runner folder
//                                              with its ZIP extracted)
//   metadata.json (optional, in either place): Runner Gallery keys
//   "displayName", "author", "type" ("monochrome" | "color"), plus the
//   optional AQTRunCat key "frameOrder" ([indices], RunCat Neo name).
//
// Fatal: missing directory, no frames, a frame that is not a decodable PNG,
// frames of different sizes, sizes or counts beyond the limits above.
// Not fatal (warnings): metadata problems, gaps or duplicates in the frame
// numbering, non-upstream dimensions, a single frame. Other files are ignored.
RunnerLoadResult loadRunner(const QString &directory, const QString &id, RunnerSource source);

// "border-collie" → "Border Collie": display name when metadata has none.
QString displayNameFromId(const QString &id);

// True when (almost) every visible pixel is grey: such runners are drawn in
// the panel text colour. Used only when metadata does not say.
bool looksMonochrome(const QList<QImage> &frames);

} // namespace aqtruncat
