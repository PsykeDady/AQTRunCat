#pragma once

#include "appsettings.h"
#include "metrics/systemmetrics.h"
#include "runner/runner.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class RunnerPreviewWidget;
class CustomMetricsModel;
class ProviderIntegration;

struct RunnerOption {
    QString id;
    QString displayName;
    aqtruncat::RunnerSource source = aqtruncat::RunnerSource::Bundled;
};

class SettingsWindow : public QWidget
{
    Q_OBJECT

public:
    // claude/codex: the official integrations (F2 Claude Code, F3 Codex);
    // nullptr hides that panel (and the section when both are null).
    SettingsWindow(aqtruncat::AppSettings *settings, QList<RunnerOption> runners,
                   CustomMetricsModel *customMetrics = nullptr, QWidget *parent = nullptr,
                   ProviderIntegration *claude = nullptr, ProviderIntegration *codex = nullptr);

    void showAndRaise();
    void setActiveRunner(const aqtruncat::Runner &runner);
    void setAvailableInterfaces(const std::vector<aqtruncat::metrics::InterfaceMetrics> &interfaces);
    // F5: the GPUs the backend reports, for the Metrics Bar GPU choice.
    void setAvailableGpus(const std::vector<aqtruncat::metrics::GpuDevice> &gpus);
    bool addCustomMetricFile(const QString &path, QString *error = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void syncFromSettings();
    void rebuildInterfaces();
    void rebuildGpus();
    void storeCheckedInterfaces();
    void rebuildCustomMetrics();
    void storeCustomMetricEnabled(QTreeWidgetItem *item);
    void applyCustomMetricEnabled(const QString &path, bool enabled);
    void rebuildMetricsBar();
    void storeMetricsBarItems();
    void moveMetricsBarItem(int delta);
    void syncClaude();
    void updateIntegrationAvailability(); // both integrations' status lines
    void syncCodex();
    QString integrationAvailability(const ProviderIntegration *integration, const QString &adapter,
                                    const QString &provider, const QString &foundKey) const;
    QStringList claudeEnableArguments() const;
    QString integrationMetricFile() const;

    aqtruncat::AppSettings *m_settings = nullptr;
    CustomMetricsModel *m_customMetrics = nullptr;
    QList<RunnerOption> m_runners;
    QString m_activeRunnerId;
    std::vector<aqtruncat::metrics::InterfaceMetrics> m_availableInterfaces;
    std::vector<std::pair<std::string, std::string>> m_availableGpus; // id, model
    bool m_rebuilding = false;
    bool m_rebuildingCustomMetrics = false;

    QComboBox *m_runnerCombo = nullptr;
    RunnerPreviewWidget *m_preview = nullptr;
    QRadioButton *m_normal = nullptr;
    QRadioButton *m_slow = nullptr;
    QRadioButton *m_networkAutomatic = nullptr;
    QRadioButton *m_networkManual = nullptr;
    QTreeWidget *m_interfaces = nullptr;
    QComboBox *m_refresh = nullptr;
    QComboBox *m_gpuDevice = nullptr;
    QComboBox *m_fpsSource = nullptr;
    QComboBox *m_updatesInterval = nullptr;
    QCheckBox *m_updatesRemote = nullptr;
    QCheckBox *m_autostart = nullptr;
    QTreeWidget *m_customMetricFiles = nullptr;
    QPushButton *m_removeCustomMetric = nullptr;
    QCheckBox *m_metricsBarEnabled = nullptr;
    QTreeWidget *m_metricsBarItems = nullptr;
    QPushButton *m_metricsBarUp = nullptr;
    QPushButton *m_metricsBarDown = nullptr;
    QLabel *m_metricsBarCount = nullptr;

    ProviderIntegration *m_claude = nullptr;
    bool m_claudeMigrated = false;
    QCheckBox *m_claudeEnabled = nullptr;
    QLabel *m_claudeStatus = nullptr;
    QCheckBox *m_claudeLive = nullptr;
    QLabel *m_claudeLiveNote = nullptr;
    QCheckBox *m_claudeContext = nullptr;
    QLineEdit *m_claudeCwd = nullptr;
    QPushButton *m_claudeBrowse = nullptr;
    QLabel *m_claudeMessage = nullptr;

    ProviderIntegration *m_codex = nullptr;
    QCheckBox *m_codexEnabled = nullptr;
    QLabel *m_codexStatus = nullptr;
    QCheckBox *m_codexLive = nullptr;
    QLabel *m_codexLiveNote = nullptr;
    QLabel *m_codexMessage = nullptr;
};
