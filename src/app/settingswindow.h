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
class QPushButton;
class RunnerPreviewWidget;
class CustomMetricsService;
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
    // claude: the Claude Code integration (F2); nullptr hides the section.
    SettingsWindow(aqtruncat::AppSettings *settings, QList<RunnerOption> runners,
                   CustomMetricsService *customMetrics = nullptr, QWidget *parent = nullptr,
                   ProviderIntegration *claude = nullptr);

    void showAndRaise();
    void setActiveRunner(const aqtruncat::Runner &runner);
    void setAvailableInterfaces(const std::vector<aqtruncat::metrics::InterfaceMetrics> &interfaces);
    bool addCustomMetricFile(const QString &path, QString *error = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void syncFromSettings();
    void rebuildInterfaces();
    void storeCheckedInterfaces();
    void rebuildCustomMetrics();
    void rebuildMetricsBar();
    void storeMetricsBarItems();
    void moveMetricsBarItem(int delta);
    void syncClaude();
    void updateClaudeAvailability();
    QStringList claudeEnableArguments() const;
    QString integrationMetricFile() const;

    aqtruncat::AppSettings *m_settings = nullptr;
    CustomMetricsService *m_customMetrics = nullptr;
    QList<RunnerOption> m_runners;
    QString m_activeRunnerId;
    std::vector<aqtruncat::metrics::InterfaceMetrics> m_availableInterfaces;
    bool m_rebuilding = false;

    QComboBox *m_runnerCombo = nullptr;
    RunnerPreviewWidget *m_preview = nullptr;
    QRadioButton *m_normal = nullptr;
    QRadioButton *m_slow = nullptr;
    QRadioButton *m_networkAutomatic = nullptr;
    QRadioButton *m_networkManual = nullptr;
    QTreeWidget *m_interfaces = nullptr;
    QComboBox *m_refresh = nullptr;
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
};
