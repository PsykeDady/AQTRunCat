#pragma once

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

namespace aqtruncat {

enum class RunnerSource {
    Bundled, // shipped inside AQTRunCat
    User,    // installed by the user under $XDG_DATA_HOME/AQTRunCat/runners
};

QString sourceName(RunnerSource source);

// A loaded, validated runner: everything needed to animate it, nothing about
// how fast (that comes from the CPU pipeline) or where it is shown.
struct Runner {
    QString id;          // stable identifier, e.g. "cat" or "beagle"
    QString displayName; // human readable name
    QString author;      // optional, from metadata
    RunnerSource source = RunnerSource::Bundled;
    QString path;        // directory the frames were loaded from

    QList<QImage> frames;  // decoded once at load time, all frameSize
    QStringList frameFiles; // the file of each frame (same index as frames)
    QList<int> sequence;   // playback order as indices into frames
    QSize frameSize;       // any aspect ratio; upstream runners are N×36
    bool monochrome = true; // template semantics: alpha kept, colour from the panel

    QStringList warnings;  // non-fatal oddities found while loading
};

} // namespace aqtruncat
