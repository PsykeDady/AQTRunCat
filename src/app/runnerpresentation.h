#pragma once

#include <QColor>
#include <QImage>
#include <QPalette>

// Shared tray/preview template presentation. Colour runners are untouched;
// monochrome runners retain alpha and receive the active palette text colour.
QImage presentRunnerFrame(const QImage &frame, bool monochrome, const QColor &colour);


// Foreground for tray content (monochrome runners, Metrics Bar text): the
// palette's WindowText, unless the platform reports a colour scheme that the
// palette contradicts. Generic Qt on non-KDE desktops (e.g. the GNOME theme)
// reports Dark while keeping a light palette with black text, which would be
// invisible on a dark panel; the palette's own Window colour is used then.
// No colour is hard-coded.
QColor trayForegroundColour(const QPalette &palette, Qt::ColorScheme scheme);
// The same for the running application (QGuiApplication palette and style hints).
QColor trayForegroundColour();
