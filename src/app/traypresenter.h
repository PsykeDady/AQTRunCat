#pragma once

#include "runner/runner.h"

#include <QIcon>
#include <QList>
#include <QMenu>
#include <QObject>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTimer>

// Shows runner frames in the system tray. Knows neither the CPU nor where
// runners come from: it gets a Runner and the index of the frame to show.
class TrayPresenter : public QObject
{
    Q_OBJECT

public:
    explicit TrayPresenter(QObject *parent = nullptr);

    // Prepares one icon per frame (tinted for monochrome runners) and drops
    // everything belonging to the previous runner.
    void setRunner(const aqtruncat::Runner &runner);

    void showFrame(int frameIndex);
    void setDashboardVisible(bool visible);

    // F6: the hover summary under the title (hoversummary.h). Sent to the
    // tray only when the text changes, together with the next frame.
    void setSummary(const QStringList &lines);

    const QMenu &menu() const { return m_menu; }
    QString toolTip() const { return m_tray.toolTip(); }
    int toolTipUpdates() const { return m_toolTipUpdates; }

signals:
    void dashboardToggleRequested();
    void settingsRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuildIcons();
    void trayActivated(QSystemTrayIcon::ActivationReason reason);
    void updateToolTip(bool now);
    void applyToolTip();

    QList<QImage> m_frames; // implicitly shared with the Runner
    bool m_monochrome = true;
    QList<QIcon> m_icons;
    int m_frame = 0;
    QSystemTrayIcon m_tray;
    QMenu m_menu;
    QAction *m_dashboardAction = nullptr;
    QString m_runnerName;
    QStringList m_summary;
    QString m_toolTip;
    bool m_toolTipPending = false;
    QTimer m_toolTipFallback; // no frame shown soon: send it anyway
    int m_toolTipUpdates = 0;
};
