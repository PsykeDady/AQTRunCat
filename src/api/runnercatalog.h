#pragma once

#include "runner/runnerservice.h"

#include <QJsonObject>
#include <QList>
#include <QString>

// F4.2: the runner catalogue of the API (GetRunners, capability "runners").
// A frontend that cannot use the Qt runner library (the Plasma Plasmoid, a
// GTK or Waybar client) animates the runner locally from these files: the
// backend never sends frames, only where they are and in which order they
// play. Kept out of aqtruncat_api because loading runners needs QtGui
// (QImageReader, for validation and the monochrome test); only aqtruncatd
// links it.
namespace aqtruncat::api {

// Payload of GetRunners: every runner AQTRunCat finds (bundled and user),
// valid or not, and which one a frontend shows for the stored runnerId
// ("selected", the same fallback as the Qt frontend at start-up).
//   {schema, requested, selected, defaultRunnerId,
//    runners: [{id, displayName, author, source, valid, error, monochrome,
//               frameWidth, frameHeight, frames: [absolute paths],
//               sequence: [indices into frames, playback order], warnings}]}
QJsonObject runnerCatalog(const RunnerService &runners, const QString &requestedId, const QString &defaultId);

} // namespace aqtruncat::api
