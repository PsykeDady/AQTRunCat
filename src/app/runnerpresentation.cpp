#include "runnerpresentation.h"

#include <QGuiApplication>
#include <QPainter>
#include <QStyleHints>

QImage presentRunnerFrame(const QImage &frame, bool monochrome, const QColor &colour)
{
    if (!monochrome)
        return frame;
    QImage tinted = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&tinted);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), colour);
    return tinted;
}


QColor trayForegroundColour(const QPalette &palette, Qt::ColorScheme scheme)
{
    const QColor text = palette.color(QPalette::Active, QPalette::WindowText);
    const bool darkText = text.lightnessF() < 0.5;
    if ((scheme == Qt::ColorScheme::Dark && darkText) || (scheme == Qt::ColorScheme::Light && !darkText))
        return palette.color(QPalette::Active, QPalette::Window);
    return text;
}

QColor trayForegroundColour()
{
    return trayForegroundColour(QGuiApplication::palette(), QGuiApplication::styleHints()->colorScheme());
}
