#pragma once

#include "metrics/systemmetrics.h"
#include "custommetrics/custommetric.h"
#include "custommetricsmodel.h"

#include <QHash>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

class QCloseEvent;
class QScrollArea;

// Compact, theme-native view of SystemMetricsSnapshot. It owns no sampler:
// hiding or closing this window never affects metrics or runner lifecycles.
class DashboardWindow : public QWidget
{
    Q_OBJECT

public:
    explicit DashboardWindow(std::string homePath, QWidget *parent = nullptr);

    void toggleVisible();

public slots:
    void updateSnapshot(const aqtruncat::metrics::SystemMetricsSnapshot &snapshot);
    void setBuiltInMetricVisibility(bool cpuTemperature, bool gpuMemory, bool gpuTemperature, bool fps);
    void updateCustomMetrics(const QList<aqtruncat::custommetrics::Source> &sources);
    // v2F1.7: the answer to a refreshRequested, shown briefly as a tooltip
    // where the icon was clicked (refreshFeedback keeps the text).
    void showRefreshResult(const QString &sourceId, const aqtruncat::custommetrics::RefreshResult &result);

public:
    QString refreshFeedback() const { return m_refreshFeedback; }

signals:
    void visibilityChanged(bool visible);
    // v2F1.7: a Custom Metric's or a group's icon was clicked (Source::id).
    void refreshRequested(const QString &sourceId);

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct StorageRow {
        std::string path;
        QLabel *value = nullptr;
        QLabel *detail = nullptr;
        QProgressBar *bar = nullptr;
    };

    static QWidget *sectionHeader(const QString &title, QWidget *parent);
    static void setProgress(QProgressBar *bar, const std::optional<double> &percent,
                            const QString &unavailable = QStringLiteral("N/A"));
    static QString statusText(const aqtruncat::metrics::MetricStatus &status);
    StorageRow &ensureStorageRow(const std::string &path);

    // F5
    struct TextRow {
        QWidget *container = nullptr;
        QLabel *value = nullptr;
        QLabel *detail = nullptr;
        QLabel *warning = nullptr;
    };
    struct GpuCard {
        std::string id;
        QWidget *card = nullptr;
        QLabel *title = nullptr;
        QLabel *detail = nullptr;
        QLabel *utilizationName = nullptr;
        QLabel *utilization = nullptr;
        QProgressBar *utilizationBar = nullptr;
        QLabel *memory = nullptr;
        QLabel *memoryName = nullptr;
        QProgressBar *memoryBar = nullptr;
        QLabel *temperature = nullptr;
        QLabel *temperatureName = nullptr;
    };
    TextRow textRow(const QString &title, const char *name, QWidget *parent);
    static void setText(TextRow &row, const QString &value, const QString &detail, bool warning = false,
                        const QString &tooltip = QString());
    void updateGpus(const aqtruncat::metrics::GpuMetrics &gpu);
    void updateUpdates(TextRow &row, const aqtruncat::metrics::UpdatesMetrics &updates);
    void rebuildCustomMetrics();
    QWidget *customCard(const aqtruncat::custommetrics::Source &source, int index);
    // v2F1.5: a group card (title, icon, its executable descendants as
    // compact cells that wrap), and those descendants in order.
    QWidget *customGroupCard(const aqtruncat::custommetrics::Source &group, int index);
    QList<const aqtruncat::custommetrics::Source *> groupMembers(const aqtruncat::custommetrics::Source &group) const;
    // What a card presents: the state (the settled one during a refresh) and
    // the text of its status line; and a key of everything the card shows,
    // so an unchanged card is left alone.
    aqtruncat::custommetrics::State presentedState(const aqtruncat::custommetrics::Source &source,
                                                   QString *message) const;
    QString customCardKey(const aqtruncat::custommetrics::Source &source) const;
    // v2F1.7: makes a card's (or a cell's) icon request a refresh of source.
    void makeRefreshTarget(QLabel *icon, const aqtruncat::custommetrics::Source &source);

    std::string m_homePath;
    QVBoxLayout *m_layout = nullptr;
    QVBoxLayout *m_storageLayout = nullptr;
    // Official provider integrations (Source::integration set) and the
    // user's own Custom Metrics share the card renderer but not the section;
    // an empty section is hidden with its heading.
    QWidget *m_integrationsHeader = nullptr;
    QWidget *m_integrationsContainer = nullptr;
    QVBoxLayout *m_integrationsLayout = nullptr;
    QWidget *m_customHeader = nullptr;
    QWidget *m_customContainer = nullptr;
    QVBoxLayout *m_customLayout = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_cpuHeader = nullptr;
    QWidget *m_cpuTop = nullptr;
    QWidget *m_gpuHeader = nullptr;
    bool m_showCpuTemperature = true;
    bool m_showGpuMemory = true;
    bool m_showGpuTemperature = true;
    bool m_showFps = true;
    std::optional<aqtruncat::metrics::SystemMetricsSnapshot> m_lastSnapshot;
    bool m_shownOnce = false;
    // Command sources publish on every run; a hidden Dashboard keeps only the
    // latest list and rebuilds its cards when shown.
    QList<aqtruncat::custommetrics::Source> m_customSources;
    bool m_customDirty = false;
    // The cards on screen, in order (integrations, then custom), with the
    // source and presentation key each was built from: a refresh replaces
    // only the cards whose key changed; a different list rebuilds them all.
    struct CustomCard {
        QString source;
        bool official = false;
        QString key;
        QWidget *widget = nullptr;
    };
    QList<CustomCard> m_customCards;
    // Presentation only: the last settled (non-Pending) state of each source
    // that holds a valid value. A refresh keeps the source Pending, but its
    // card keeps showing this state instead of flickering to "Waiting…".
    struct SettledState {
        aqtruncat::custommetrics::State state = aqtruncat::custommetrics::State::Available;
        QString message;
    };
    QHash<QString, SettledState> m_customSettled;
    // v2F1.7: where the last refresh was requested, and its answer.
    QPoint m_refreshPoint;
    QString m_refreshFeedback;

    QLabel *m_cpuValue = nullptr;
    QLabel *m_cpuDetail = nullptr;
    QProgressBar *m_cpuBar = nullptr;
    QLabel *m_memoryValue = nullptr;
    QLabel *m_memoryDetail = nullptr;
    QProgressBar *m_memoryBar = nullptr;
    QLabel *m_swapValue = nullptr;
    QLabel *m_networkDown = nullptr;
    QLabel *m_networkUp = nullptr;
    QLabel *m_diskRead = nullptr;
    QLabel *m_diskWrite = nullptr;
    QList<StorageRow> m_storageRows;

    TextRow m_uptime;
    TextRow m_kernel;
    TextRow m_distribution;
    QVBoxLayout *m_gpuLayout = nullptr;
    QLabel *m_gpuStatus = nullptr;
    QList<GpuCard> m_gpuCards;
    QWidget *m_gamingHeader = nullptr; // hidden with FPS when its source is OFF
    TextRow m_fps;
    TextRow m_cpuTemperature;
    TextRow m_packageUpdates;
    TextRow m_flatpakUpdates;
};
