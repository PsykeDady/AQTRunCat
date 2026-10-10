#include "traypresenter.h"

#include "applicationquit.h"
#include "runnerpresentation.h"

#include <QApplication>
#include <QPalette>
#include <QStyleHints>

TrayPresenter::TrayPresenter(QObject *parent)
    : QObject(parent)
{
    m_dashboardAction = m_menu.addAction(tr("Show Dashboard"), this, &TrayPresenter::dashboardToggleRequested);
    m_menu.addAction(tr("Settings…"), this, &TrayPresenter::settingsRequested);
    m_menu.addSeparator();
    m_menu.addAction(tr("Quit AQTRunCat"), this, [] { requestApplicationQuit(); });
    m_tray.setContextMenu(&m_menu);
    m_tray.setToolTip(QStringLiteral("AQTRunCat"));
    connect(&m_tray, &QSystemTrayIcon::activated, this, &TrayPresenter::trayActivated);

    // Monochrome runners follow the panel text colour, also when the colour
    // scheme changes at runtime (some platforms only report the scheme).
    qApp->installEventFilter(this);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] { rebuildIcons(); });
}

void TrayPresenter::setDashboardVisible(bool visible)
{
    m_dashboardAction->setText(visible ? tr("Hide Dashboard") : tr("Show Dashboard"));
}

void TrayPresenter::trayActivated(QSystemTrayIcon::ActivationReason reason)
{
    // Plasma/SNI reports a normal left click as Trigger. Context is handled
    // by the exported menu; double/middle clicks are intentionally ignored.
    if (reason == QSystemTrayIcon::Trigger)
        emit dashboardToggleRequested();
}

void TrayPresenter::setRunner(const aqtruncat::Runner &runner)
{
    m_frames = runner.frames;
    m_monochrome = runner.monochrome;
    m_frame = 0;
    rebuildIcons();
    m_tray.setToolTip(QStringLiteral("AQTRunCat — %1").arg(runner.displayName));
    if (!m_tray.isVisible())
        m_tray.show();
}

void TrayPresenter::showFrame(int frameIndex)
{
    if (frameIndex < 0 || frameIndex >= m_icons.size())
        return;
    m_frame = frameIndex;
    m_tray.setIcon(m_icons[frameIndex]);
}

void TrayPresenter::rebuildIcons()
{
    // Built once per runner (and again on palette change); animation ticks only
    // swap prepared icons. Images keep their size and aspect ratio: the tray
    // host scales them into its slot.
    const QColor color = trayForegroundColour();
    m_icons.clear();
    for (const QImage &frame : std::as_const(m_frames)) {
        m_icons.append(QIcon(QPixmap::fromImage(presentRunnerFrame(frame, m_monochrome, color))));
    }
    if (!m_icons.isEmpty())
        m_tray.setIcon(m_icons[qBound(0, m_frame, int(m_icons.size()) - 1)]);
}

bool TrayPresenter::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange)
        rebuildIcons();
    return QObject::eventFilter(watched, event);
}
