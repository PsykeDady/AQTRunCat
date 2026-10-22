#pragma once

#include "metrics/systemmetrics.h"
#include "custommetrics/custommetric.h"

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
    void updateCustomMetrics(const QList<aqtruncat::custommetrics::Source> &sources);

signals:
    void visibilityChanged(bool visible);

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

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
        QProgressBar *memoryBar = nullptr;
        QLabel *temperature = nullptr;
    };
    TextRow textRow(const QString &title, const char *name, QWidget *parent);
    static void setText(TextRow &row, const QString &value, const QString &detail, bool warning = false,
                        const QString &tooltip = QString());
    void updateGpus(const aqtruncat::metrics::GpuMetrics &gpu);
    void updateUpdates(TextRow &row, const aqtruncat::metrics::UpdatesMetrics &updates);
    void rebuildCustomMetrics();
    QWidget *customCard(const aqtruncat::custommetrics::Source &source, int index);

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
    bool m_shownOnce = false;
    // Command sources publish on every run; a hidden Dashboard keeps only the
    // latest list and rebuilds its cards when shown.
    QList<aqtruncat::custommetrics::Source> m_customSources;
    bool m_customDirty = false;
    // Presentation only: the last settled (non-Pending) state of each source
    // that holds a valid value. A refresh keeps the source Pending, but its
    // card keeps showing this state instead of flickering to "Waiting…".
    struct SettledState {
        aqtruncat::custommetrics::State state = aqtruncat::custommetrics::State::Available;
        QString message;
    };
    QHash<QString, SettledState> m_customSettled;

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
    TextRow m_packageUpdates;
    TextRow m_flatpakUpdates;
};
