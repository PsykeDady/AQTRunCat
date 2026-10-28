#include "settingswindow.h"

#include "dependencies/dependencyadvisor.h"

#include "runnerpreviewwidget.h"
#include "custommetricsmodel.h"
#include "metricsbarmodel.h"
#include "providerintegration.h"
#include "custommetrics/commandmetric.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QDir>
#include <QFormLayout>
#include <QFrame>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

using aqtruncat::metrics::InterfaceKind;
using aqtruncat::metrics::NetworkPolicyMode;

namespace {

QString kindName(InterfaceKind kind)
{
    switch (kind) {
    case InterfaceKind::Loopback:
        return SettingsWindow::tr("Loopback");
    case InterfaceKind::Physical:
        return SettingsWindow::tr("Physical");
    case InterfaceKind::Virtual:
        return SettingsWindow::tr("Virtual");
    }
    return SettingsWindow::tr("Unknown");
}

QGroupBox *group(const QString &title, QWidget *parent)
{
    auto *box = new QGroupBox(title, parent);
    box->setFlat(false);
    return box;
}

} // namespace

SettingsWindow::SettingsWindow(aqtruncat::AppSettings *settings, QList<RunnerOption> runners,
                               CustomMetricsModel *customMetrics, QWidget *parent,
                               ProviderIntegration *claude, ProviderIntegration *codex)
    : QWidget(parent, Qt::Tool)
    , m_settings(settings)
    , m_customMetrics(customMetrics)
    , m_runners(std::move(runners))
    , m_claude(claude)
    , m_codex(codex)
{
    setObjectName(QStringLiteral("settingsWindow"));
    setWindowTitle(tr("AQTRunCat Settings"));
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumWidth(420);
    resize(480, 760);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *contents = new QWidget(scroll);
    auto *root = new QVBoxLayout(contents);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);
    scroll->setWidget(contents);
    outer->addWidget(scroll);

    auto *runnerBox = group(tr("Runner"), this);
    auto *runnerLayout = new QVBoxLayout(runnerBox);
    m_runnerCombo = new QComboBox(runnerBox);
    m_runnerCombo->setObjectName(QStringLiteral("runnerCombo"));
    for (const RunnerOption &runner : std::as_const(m_runners)) {
        const QString suffix = runner.source == aqtruncat::RunnerSource::User ? tr(" — User") : QString();
        m_runnerCombo->addItem(runner.displayName + suffix, runner.id);
    }
    runnerLayout->addWidget(m_runnerCombo);
    m_preview = new RunnerPreviewWidget(runnerBox);
    runnerLayout->addWidget(m_preview);
    root->addWidget(runnerBox);

    auto *speedBox = group(tr("Speed"), this);
    auto *speedLayout = new QVBoxLayout(speedBox);
    m_normal = new QRadioButton(tr("Normal — faster under load"), speedBox);
    m_normal->setObjectName(QStringLiteral("normalSpeed"));
    m_slow = new QRadioButton(tr("Slow down under load"), speedBox);
    m_slow->setObjectName(QStringLiteral("slowSpeed"));
    speedLayout->addWidget(m_normal);
    speedLayout->addWidget(m_slow);
    root->addWidget(speedBox);

    auto *networkBox = group(tr("Network"), this);
    auto *networkLayout = new QVBoxLayout(networkBox);
    m_networkAutomatic = new QRadioButton(tr("Automatic — physical interfaces"), networkBox);
    m_networkAutomatic->setObjectName(QStringLiteral("networkAutomatic"));
    m_networkManual = new QRadioButton(tr("Manual — exactly selected interfaces"), networkBox);
    m_networkManual->setObjectName(QStringLiteral("networkManual"));
    networkLayout->addWidget(m_networkAutomatic);
    networkLayout->addWidget(m_networkManual);
    m_interfaces = new QTreeWidget(networkBox);
    m_interfaces->setObjectName(QStringLiteral("networkInterfaces"));
    m_interfaces->setHeaderLabels({tr("Interface"), tr("Type / status")});
    m_interfaces->setRootIsDecorated(false);
    m_interfaces->setUniformRowHeights(true);
    networkLayout->addWidget(m_interfaces);
    root->addWidget(networkBox);

    // F5 built-in metrics: only the choices that have no single right answer.
    auto *builtInBox = group(tr("Built-in Metrics"), this);
    builtInBox->setObjectName(QStringLiteral("builtInMetricsBox"));
    auto *builtInForm = new QFormLayout(builtInBox);
    m_gpuDevice = new QComboBox(builtInBox);
    m_gpuDevice->setObjectName(QStringLiteral("gpuDevice"));
    m_gpuDevice->setToolTip(tr("The GPU whose utilization, memory and temperature the Metrics Bar shows. "
                               "The Dashboard always lists every GPU."));
    builtInForm->addRow(tr("Metrics Bar GPU:"), m_gpuDevice);
    m_fpsSource = new QComboBox(builtInBox);
    m_fpsSource->setObjectName(QStringLiteral("fpsSource"));
    m_fpsSource->addItem(tr("MangoHud log"), QStringLiteral("mangohud"));
    m_fpsSource->addItem(tr("Off"), QStringLiteral("off"));
    builtInForm->addRow(tr("FPS source:"), m_fpsSource);
    m_showCpuTemperature = new QCheckBox(tr("Show CPU temperature"), builtInBox);
    m_showCpuTemperature->setObjectName(QStringLiteral("showCpuTemperature"));
    builtInForm->addRow(m_showCpuTemperature);
    m_showGpuMemory = new QCheckBox(tr("Show GPU memory"), builtInBox);
    m_showGpuMemory->setObjectName(QStringLiteral("showGpuMemory"));
    builtInForm->addRow(m_showGpuMemory);
    m_showGpuTemperature = new QCheckBox(tr("Show GPU temperature"), builtInBox);
    m_showGpuTemperature->setObjectName(QStringLiteral("showGpuTemperature"));
    builtInForm->addRow(m_showGpuTemperature);
    m_showFps = new QCheckBox(tr("Show FPS"), builtInBox);
    m_showFps->setObjectName(QStringLiteral("showFps"));
    builtInForm->addRow(m_showFps);
    auto *fpsNote = new QLabel(tr("FPS is read from the CSV log MangoHud writes while it logs a game "
                                  "(output_folder and log_interval in MangoHud.conf). AQTRunCat never "
                                  "injects anything into games."), builtInBox);
    fpsNote->setWordWrap(true);
    builtInForm->addRow(fpsNote);
    m_updatesInterval = new QComboBox(builtInBox);
    m_updatesInterval->setObjectName(QStringLiteral("updatesInterval"));
    for (const int minutes : aqtruncat::AppSettings::updatesIntervals()) {
        const QString text = minutes == 0  ? tr("Off")
            : minutes < 60                 ? tr("Every %1 minutes").arg(minutes)
            : minutes == 60                ? tr("Every hour")
            : minutes == 1440              ? tr("Once a day")
                                           : tr("Every %1 hours").arg(minutes / 60);
        m_updatesInterval->addItem(text, minutes);
    }
    builtInForm->addRow(tr("Check for updates:"), m_updatesInterval);
    m_updatesRemote = new QCheckBox(tr("Download fresh package metadata (uses the network)"), builtInBox);
    m_updatesRemote->setObjectName(QStringLiteral("updatesRemote"));
    m_updatesRemote->setToolTip(tr("Off: only cached metadata is read; on Arch, checkupdates uses its private "
                                   "database without syncing. On: checkupdates syncs only that private database, "
                                   "dnf refreshes its own cache and Flatpak downloads "
                                   "remote summaries, at most once an hour. apt cannot refresh without root. "
                                   "Arch requires checkupdates from pacman-contrib; AUR updates are not included. "
                                   "Nothing is ever installed."));
    builtInForm->addRow(m_updatesRemote);
    root->addWidget(builtInBox);

    auto *dependenciesBox = group(tr("Dependency Advisor"), this);
    auto *dependenciesLayout = new QVBoxLayout(dependenciesBox);
    m_checkDependencies = new QPushButton(tr("Check enabled features"), dependenciesBox);
    m_checkDependencies->setObjectName(QStringLiteral("checkDependencies"));
    dependenciesLayout->addWidget(m_checkDependencies);
    m_dependencyReport = new QLabel(tr("Checks are read-only and do not install software."), dependenciesBox);
    m_dependencyReport->setObjectName(QStringLiteral("dependencyReport"));
    m_dependencyReport->setWordWrap(true);
    m_dependencyReport->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dependenciesLayout->addWidget(m_dependencyReport);
    connect(m_checkDependencies, &QPushButton::clicked, this, [this] {
        m_dependencyReport->setText(aqtruncat::dependencies::formatReport(
            aqtruncat::dependencies::currentUserOptions()));
    });
    root->addWidget(dependenciesBox);

    QGroupBox *integrationsBox = nullptr;
    QVBoxLayout *integrations = nullptr;
    auto providerName = [&](const QString &text) {
        auto *name = new QLabel(text, integrationsBox);
        QFont bold = name->font();
        bold.setBold(true);
        name->setFont(bold);
        integrations->addWidget(name);
    };
    auto secondaryLabel = [&](const char *objectName) {
        auto *label = new QLabel(integrationsBox);
        label->setObjectName(QLatin1String(objectName));
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        integrations->addWidget(label);
        return label;
    };
    if (m_claude || m_codex) {
        // Official integrations: AQTRunCat-supported providers whose adapter
        // owns their configuration (see ProviderIntegration), as opposed to
        // the user-defined Custom Metrics below.
        integrationsBox = group(tr("Integrations"), this);
        integrationsBox->setObjectName(QStringLiteral("integrationsGroup"));
        integrations = new QVBoxLayout(integrationsBox);
    }
    if (m_claude) {
        auto *name = new QLabel(tr("Claude Code"), integrationsBox);
        QFont bold = name->font();
        bold.setBold(true);
        name->setFont(bold);
        integrations->addWidget(name);
        auto *description = new QLabel(
            tr("Your Claude plan usage as on Claude Code's /usage screen: the current 5-hour session "
               "window and the current week (all models), with their reset times, in the Dashboard "
               "and, if you select them, in the Metrics Bar."),
            integrationsBox);
        description->setWordWrap(true);
        integrations->addWidget(description);
        m_claudeEnabled = new QCheckBox(tr("Show Claude Code usage"), integrationsBox);
        m_claudeEnabled->setObjectName(QStringLiteral("claudeEnabled"));
        integrations->addWidget(m_claudeEnabled);
        m_claudeStatus = new QLabel(integrationsBox);
        m_claudeStatus->setObjectName(QStringLiteral("claudeStatus"));
        m_claudeStatus->setWordWrap(true);
        m_claudeStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
        integrations->addWidget(m_claudeStatus);
        m_claudeLive = new QCheckBox(tr("Live updates through Claude Code's status line"), integrationsBox);
        m_claudeLive->setObjectName(QStringLiteral("claudeLiveUpdates"));
        integrations->addWidget(m_claudeLive);
        m_claudeLiveNote = new QLabel(integrationsBox);
        m_claudeLiveNote->setObjectName(QStringLiteral("claudeLiveNote"));
        m_claudeLiveNote->setWordWrap(true);
        QFont secondary = m_claudeLiveNote->font();
        secondary.setPointSizeF(secondary.pointSizeF() * 0.9);
        m_claudeLiveNote->setFont(secondary);
        integrations->addWidget(m_claudeLiveNote);
        m_claudeContext = new QCheckBox(tr("Also show the context window of the most recently active session"),
                                        integrationsBox);
        m_claudeContext->setObjectName(QStringLiteral("claudeContext"));
        integrations->addWidget(m_claudeContext);
        auto *cwdRow = new QHBoxLayout;
        cwdRow->addWidget(new QLabel(tr("Only sessions in:"), integrationsBox));
        m_claudeCwd = new QLineEdit(integrationsBox);
        m_claudeCwd->setObjectName(QStringLiteral("claudeCwd"));
        m_claudeCwd->setPlaceholderText(tr("any project"));
        m_claudeCwd->setClearButtonEnabled(true);
        cwdRow->addWidget(m_claudeCwd, 1);
        m_claudeBrowse = new QPushButton(tr("Browse…"), integrationsBox);
        m_claudeBrowse->setObjectName(QStringLiteral("claudeBrowse"));
        cwdRow->addWidget(m_claudeBrowse);
        integrations->addLayout(cwdRow);
        m_claudeMessage = new QLabel(integrationsBox);
        m_claudeMessage->setObjectName(QStringLiteral("claudeMessage"));
        m_claudeMessage->setWordWrap(true);
        m_claudeMessage->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_claudeMessage->hide();
        integrations->addWidget(m_claudeMessage);
    }
    if (m_codex) {
        if (m_claude) {
            auto *separator = new QFrame(integrationsBox);
            separator->setFrameShape(QFrame::HLine);
            separator->setFrameShadow(QFrame::Sunken);
            integrations->addWidget(separator);
        }
        providerName(tr("Codex"));
        auto *description = new QLabel(
            tr("Your Codex usage limits as Codex's /status shows them (for example the 5-hour and the "
               "weekly limit), with their reset times, in the Dashboard and, if you select them, in the "
               "Metrics Bar."),
            integrationsBox);
        description->setWordWrap(true);
        integrations->addWidget(description);
        m_codexEnabled = new QCheckBox(tr("Show Codex usage"), integrationsBox);
        m_codexEnabled->setObjectName(QStringLiteral("codexEnabled"));
        integrations->addWidget(m_codexEnabled);
        m_codexStatus = secondaryLabel("codexStatus");
        m_codexLive = new QCheckBox(tr("Live updates from Codex every 5 minutes"), integrationsBox);
        m_codexLive->setObjectName(QStringLiteral("codexLiveUpdates"));
        integrations->addWidget(m_codexLive);
        m_codexLiveNote = secondaryLabel("codexLiveNote");
        QFont secondary = m_codexLiveNote->font();
        secondary.setPointSizeF(secondary.pointSizeF() * 0.9);
        m_codexLiveNote->setFont(secondary);
        m_codexMessage = secondaryLabel("codexMessage");
        m_codexMessage->hide();
    }
    if (integrationsBox)
        root->addWidget(integrationsBox);

    auto *customBox = group(tr("Custom Metrics"), this);
    auto *customLayout = new QVBoxLayout(customBox);
    auto *directory = new QLabel(customBox);
    directory->setObjectName(QStringLiteral("customMetricsDirectory"));
    directory->setTextInteractionFlags(Qt::TextSelectableByMouse);
    directory->setText(m_customMetrics ? tr("Auto-discovery: %1").arg(m_customMetrics->discoveryDirectory())
                                       : tr("Auto-discovery unavailable"));
    directory->setWordWrap(true);
    customLayout->addWidget(directory);
    m_customMetricFiles = new QTreeWidget(customBox);
    m_customMetricFiles->setObjectName(QStringLiteral("customMetricFiles"));
    m_customMetricFiles->setHeaderLabels({tr("Name"), tr("File"), tr("Origin"), tr("Status")});
    m_customMetricFiles->setRootIsDecorated(false); // decorated when there are groups (v2F1.5)
    m_customMetricFiles->setUniformRowHeights(true);
    m_customMetricFiles->setMinimumHeight(110);
    m_customMetricFiles->setMaximumHeight(160);
    customLayout->addWidget(m_customMetricFiles);
    auto *customButtons = new QHBoxLayout;
    auto *addCustomMetric = new QPushButton(tr("Add File…"), customBox);
    addCustomMetric->setObjectName(QStringLiteral("addCustomMetric"));
    m_removeCustomMetric = new QPushButton(tr("Remove"), customBox);
    m_removeCustomMetric->setObjectName(QStringLiteral("removeCustomMetric"));
    m_removeCustomMetric->setEnabled(false);
    // v2F1.6: rescans the files and applies what changed, without a restart.
    m_reloadCustomMetrics = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                            tr("Reload Custom Metrics"), customBox);
    m_reloadCustomMetrics->setObjectName(QStringLiteral("reloadCustomMetrics"));
    m_reloadCustomMetrics->setToolTip(tr("Read the Custom Metrics files again and apply the metrics and groups "
                                         "that were added, changed or removed."));
    m_reloadCustomMetrics->setEnabled(m_customMetrics != nullptr);
    customButtons->addWidget(addCustomMetric);
    customButtons->addWidget(m_removeCustomMetric);
    customButtons->addStretch();
    customButtons->addWidget(m_reloadCustomMetrics);
    customLayout->addLayout(customButtons);
    m_customMetricsReloadStatus = new QLabel(customBox);
    m_customMetricsReloadStatus->setObjectName(QStringLiteral("customMetricsReloadStatus"));
    m_customMetricsReloadStatus->setWordWrap(true);
    m_customMetricsReloadStatus->setTextFormat(Qt::PlainText);
    m_customMetricsReloadStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_customMetricsReloadStatus->hide();
    customLayout->addWidget(m_customMetricsReloadStatus);
    root->addWidget(customBox);

    auto *barBox = group(tr("Metrics Bar"), this);
    auto *barLayout = new QVBoxLayout(barBox);
    m_metricsBarEnabled = new QCheckBox(tr("Show the Metrics Bar in the system tray"), barBox);
    m_metricsBarEnabled->setObjectName(QStringLiteral("metricsBarEnabled"));
    barLayout->addWidget(m_metricsBarEnabled);
    m_metricsBarItems = new QTreeWidget(barBox);
    m_metricsBarItems->setObjectName(QStringLiteral("metricsBarItems"));
    m_metricsBarItems->setHeaderLabels({tr("Metric"), tr("Notes")});
    m_metricsBarItems->setRootIsDecorated(false);
    m_metricsBarItems->setUniformRowHeights(true);
    m_metricsBarItems->setMinimumHeight(150);
    barLayout->addWidget(m_metricsBarItems);
    auto *barButtons = new QHBoxLayout;
    m_metricsBarUp = new QPushButton(tr("Move Up"), barBox);
    m_metricsBarUp->setObjectName(QStringLiteral("metricsBarUp"));
    m_metricsBarDown = new QPushButton(tr("Move Down"), barBox);
    m_metricsBarDown->setObjectName(QStringLiteral("metricsBarDown"));
    barButtons->addWidget(m_metricsBarUp);
    barButtons->addWidget(m_metricsBarDown);
    barButtons->addStretch();
    barLayout->addLayout(barButtons);
    m_metricsBarCount = new QLabel(barBox);
    m_metricsBarCount->setObjectName(QStringLiteral("metricsBarCount"));
    m_metricsBarCount->setWordWrap(true);
    barLayout->addWidget(m_metricsBarCount);
    root->addWidget(barBox);

    auto *desktopBox = group(tr("Desktop"), this);
    auto *desktopForm = new QFormLayout(desktopBox);
    m_refresh = new QComboBox(desktopBox);
    m_refresh->setObjectName(QStringLiteral("dashboardRefresh"));
    m_refresh->addItem(tr("Every second"), 1);
    m_refresh->addItem(tr("Every 2 seconds"), 2);
    m_refresh->addItem(tr("Every 5 seconds"), 5);
    desktopForm->addRow(tr("Dashboard refresh:"), m_refresh);
    m_autostart = new QCheckBox(tr("Start AQTRunCat Tray Plugin automatically on login"), desktopBox);
    m_autostart->setObjectName(QStringLiteral("autostart"));
    desktopForm->addRow(m_autostart);
    root->addWidget(desktopBox);

    auto *buttons = new QDialogButtonBox(this);
    auto *restore = buttons->addButton(tr("Restore Defaults"), QDialogButtonBox::ResetRole);
    restore->setObjectName(QStringLiteral("restoreDefaults"));
    auto *close = buttons->addButton(QDialogButtonBox::Close);
    close->setObjectName(QStringLiteral("closeSettings"));
    root->addWidget(buttons);

    connect(m_runnerCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_rebuilding && index >= 0)
            m_settings->setRunnerId(m_runnerCombo->itemData(index).toString());
    });
    connect(m_normal, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked && !m_rebuilding)
            m_settings->setSpeedMode(aqtruncat::SpeedMode::Normal);
    });
    connect(m_slow, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked && !m_rebuilding)
            m_settings->setSpeedMode(aqtruncat::SpeedMode::SlowDown);
    });
    connect(m_networkAutomatic, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked && !m_rebuilding)
            m_settings->setNetworkMode(NetworkPolicyMode::Automatic);
    });
    connect(m_networkManual, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked && !m_rebuilding)
            m_settings->setNetworkMode(NetworkPolicyMode::Manual);
    });
    connect(m_interfaces, &QTreeWidget::itemChanged, this, [this] { storeCheckedInterfaces(); });
    connect(m_refresh, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_rebuilding && index >= 0)
            m_settings->setDashboardRefreshSeconds(m_refresh->itemData(index).toInt());
    });
    connect(m_autostart, &QCheckBox::toggled, m_settings, &aqtruncat::AppSettings::setAutostartEnabled);
    connect(m_gpuDevice, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_rebuilding && index >= 0)
            m_settings->setGpuDevice(m_gpuDevice->itemData(index).toString());
    });
    connect(m_fpsSource, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_rebuilding && index >= 0)
            m_settings->setFpsSource(m_fpsSource->itemData(index).toString());
    });
    connect(m_showCpuTemperature, &QCheckBox::toggled, m_settings, &aqtruncat::AppSettings::setShowCpuTemperature);
    connect(m_showGpuMemory, &QCheckBox::toggled, m_settings, &aqtruncat::AppSettings::setShowGpuMemory);
    connect(m_showGpuTemperature, &QCheckBox::toggled, m_settings, &aqtruncat::AppSettings::setShowGpuTemperature);
    connect(m_showFps, &QCheckBox::toggled, m_settings, &aqtruncat::AppSettings::setShowFps);
    connect(m_updatesInterval, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_rebuilding && index >= 0)
            m_settings->setUpdatesIntervalMinutes(m_updatesInterval->itemData(index).toInt());
    });
    connect(m_updatesRemote, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_rebuilding)
            m_settings->setUpdatesRemoteRefresh(checked);
    });
    connect(m_settings, &aqtruncat::AppSettings::systemMetricsSettingsChanged, this, [this] { syncFromSettings(); });
    connect(m_settings, &aqtruncat::AppSettings::displaySettingsChanged, this, [this] { syncFromSettings(); });
    connect(addCustomMetric, &QPushButton::clicked, this, [this] {
        if (!m_customMetrics)
            return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Add Custom Metrics Source"), {},
                                                          tr("JSON files (*.json);;All files (*)"));
        if (path.isEmpty())
            return;
        QString error;
        if (!addCustomMetricFile(path, &error))
            QMessageBox::warning(this, tr("Invalid Custom Metrics File"), error);
    });
    connect(m_customMetricFiles, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (column == 0)
            storeCustomMetricEnabled(item);
    });
    connect(m_customMetricFiles, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        m_removeCustomMetric->setEnabled(item && item->data(0, Qt::UserRole + 1).toBool());
    });
    connect(m_removeCustomMetric, &QPushButton::clicked, this, [this] {
        if (!m_customMetrics)
            return;
        if (QTreeWidgetItem *item = m_customMetricFiles->currentItem())
            m_customMetrics->unregisterFile(item->data(0, Qt::UserRole).toString());
    });
    connect(m_reloadCustomMetrics, &QPushButton::clicked, this, [this] {
        if (!m_customMetrics)
            return;
        // One request at a time; the answer re-enables the button.
        m_reloadCustomMetrics->setEnabled(false);
        m_customMetricsReloadStatus->setText(tr("Reloading Custom Metrics…"));
        m_customMetricsReloadStatus->show();
        m_customMetrics->reloadConfigurations();
    });
    connect(m_metricsBarEnabled, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_rebuilding)
            m_settings->setMetricsBarEnabled(checked);
    });
    connect(m_metricsBarItems, &QTreeWidget::itemChanged, this, [this] { storeMetricsBarItems(); });
    connect(m_metricsBarUp, &QPushButton::clicked, this, [this] { moveMetricsBarItem(-1); });
    connect(m_metricsBarDown, &QPushButton::clicked, this, [this] { moveMetricsBarItem(1); });
    connect(restore, &QPushButton::clicked, m_settings, &aqtruncat::AppSettings::restoreDefaults);
    connect(close, &QPushButton::clicked, this, &QWidget::close);

    connect(m_settings, &aqtruncat::AppSettings::runnerIdChanged, this, [this] { syncFromSettings(); });
    connect(m_settings, &aqtruncat::AppSettings::speedModeChanged, this, [this] { syncFromSettings(); });
    connect(m_settings, &aqtruncat::AppSettings::networkSettingsChanged, this, [this] { syncFromSettings(); });
    connect(m_settings, &aqtruncat::AppSettings::dashboardRefreshSecondsChanged, this, [this] { syncFromSettings(); });
    connect(m_settings, &aqtruncat::AppSettings::autostartEnabledChanged, this, [this] { syncFromSettings(); });
    // Queued: the change usually originates from this list's own itemChanged,
    // and its items must not be deleted while that signal is being emitted.
    connect(m_settings, &aqtruncat::AppSettings::metricsBarChanged, this, [this] { rebuildMetricsBar(); },
            Qt::QueuedConnection);
    if (m_customMetrics) {
        connect(m_customMetrics, &CustomMetricsModel::sourcesChanged, this, [this] {
            rebuildCustomMetrics();
            rebuildMetricsBar();
        });
        connect(m_customMetrics, &CustomMetricsModel::reloadFinished, this,
                [this](const aqtruncat::custommetrics::ReloadReport &report) {
                    m_reloadCustomMetrics->setEnabled(true);
                    m_customMetricsReloadStatus->setText(aqtruncat::custommetrics::reloadSummary(report));
                    m_customMetricsReloadStatus->show();
                });
    }
    if (m_claude) {
        connect(m_claudeEnabled, &QCheckBox::toggled, this, [this](bool checked) {
            m_claude->run(checked ? claudeEnableArguments() : QStringList{QStringLiteral("disable")});
        });
        connect(m_claudeLive, &QCheckBox::toggled, this, [this](bool checked) {
            if (checked) {
                const QString file = m_claude->status().value(QStringLiteral("settingsFile")).toString();
                const auto answer = QMessageBox::question(
                    this, tr("Turn On Live Updates"),
                    tr("Claude Code reports usage only to a status-line command. AQTRunCat will set "
                       "\"statusLine\" in\n%1\nand change nothing else in that file.\n\n"
                       "A status line you already have keeps working: it runs right after AQTRunCat's "
                       "and is put back when live updates are turned off. While a status line is "
                       "configured, Claude Code shows fewer keyboard hints in its footer.\n\n"
                       "Turn on live updates?").arg(file));
                if (answer != QMessageBox::Yes) {
                    syncClaude();
                    return;
                }
            }
            m_claude->run({QStringLiteral("live-updates"), checked ? QStringLiteral("on") : QStringLiteral("off")});
        });
        connect(m_claudeContext, &QCheckBox::toggled, this, [this] { m_claude->run(claudeEnableArguments()); });
        connect(m_claudeCwd, &QLineEdit::editingFinished, this, [this] {
            const QString current = m_claude->status().value(QStringLiteral("cwd")).toString();
            if (QDir::cleanPath(m_claudeCwd->text().trimmed()) != current
                && !(m_claudeCwd->text().trimmed().isEmpty() && current.isEmpty()))
                m_claude->run(claudeEnableArguments());
        });
        connect(m_claudeBrowse, &QPushButton::clicked, this, [this] {
            const QString directory = QFileDialog::getExistingDirectory(this, tr("Project Folder"), m_claudeCwd->text());
            if (directory.isEmpty())
                return;
            m_claudeCwd->setText(directory);
            m_claude->run(claudeEnableArguments());
        });
        connect(m_claude, &ProviderIntegration::statusChanged, this, [this] {
            syncClaude();
            rebuildCustomMetrics();
        });
        connect(m_claude, &ProviderIntegration::busyChanged, this, [this] { syncClaude(); });
        connect(m_claude, &ProviderIntegration::commandFinished, this, [this](bool ok, const QString &message) {
            m_claudeMessage->setText(ok ? message : tr("Error: %1").arg(message));
            m_claudeMessage->setVisible(!message.isEmpty());
        });
        if (m_customMetrics)
            connect(m_customMetrics, &CustomMetricsModel::sourcesChanged, this, [this] { updateIntegrationAvailability(); });
        syncClaude();
        m_claude->refresh();
    }
    if (m_codex) {
        connect(m_codexEnabled, &QCheckBox::toggled, this, [this](bool checked) {
            m_codex->run(checked ? QStringList{QStringLiteral("enable")} : QStringList{QStringLiteral("disable")});
        });
        connect(m_codexLive, &QCheckBox::toggled, this, [this](bool checked) {
            if (checked) {
                const auto answer = QMessageBox::question(
                    this, tr("Turn On Live Updates"),
                    tr("Every 5 minutes AQTRunCat will start \"codex app-server\" and ask it for your current "
                       "usage limits. Codex contacts OpenAI with its own login to answer; no prompt is sent, "
                       "no usage is consumed and nothing in your Codex configuration is changed.\n\n"
                       "Without live updates AQTRunCat shows the limits Codex recorded in its session files "
                       "the last time you used it.\n\nTurn on live updates?"));
                if (answer != QMessageBox::Yes) {
                    syncCodex();
                    return;
                }
            }
            m_codex->run({QStringLiteral("live-updates"), checked ? QStringLiteral("on") : QStringLiteral("off")});
        });
        connect(m_codex, &ProviderIntegration::statusChanged, this, [this] {
            syncCodex();
            rebuildCustomMetrics();
        });
        connect(m_codex, &ProviderIntegration::busyChanged, this, [this] { syncCodex(); });
        connect(m_codex, &ProviderIntegration::commandFinished, this, [this](bool ok, const QString &message) {
            m_codexMessage->setText(ok ? message : tr("Error: %1").arg(message));
            m_codexMessage->setVisible(!message.isEmpty());
        });
        if (m_customMetrics && !m_claude)
            connect(m_customMetrics, &CustomMetricsModel::sourcesChanged, this, [this] { updateIntegrationAvailability(); });
        syncCodex();
        m_codex->refresh();
    }
    syncFromSettings();
    rebuildCustomMetrics();
    rebuildMetricsBar();
}

void SettingsWindow::syncCodex()
{
    if (!m_codex)
        return;
    const QJsonObject &status = m_codex->status();
    const bool enabled = status.value(QStringLiteral("enabled")).toBool();
    const bool live = status.value(QStringLiteral("liveUpdates")).toString() == QLatin1String("on");
    {
        const QSignalBlocker blockEnabled(m_codexEnabled);
        const QSignalBlocker blockLive(m_codexLive);
        m_codexEnabled->setChecked(enabled);
        m_codexLive->setChecked(live);
    }
    const bool idle = m_codex->installed() && m_codex->statusKnown() && !m_codex->busy();
    m_codexEnabled->setEnabled(idle && !status.value(QStringLiteral("userPackage")).toBool());
    m_codexLive->setEnabled(idle && enabled);
    QString note;
    if (live) {
        const QDateTime last = QDateTime::fromString(status.value(QStringLiteral("lastLiveReport")).toString(), Qt::ISODate);
        note = last.isValid() ? tr("Last live report: %1.").arg(QLocale().toString(last.toLocalTime(), QLocale::ShortFormat))
                              : tr("No live report yet.");
        if (const QString error = status.value(QStringLiteral("liveError")).toString(); !error.isEmpty())
            note += u' ' + tr("The last attempt failed: %1").arg(error);
        if (status.value(QStringLiteral("codexExecutable")).toString().isEmpty())
            note += u' ' + tr("The codex executable was not found in PATH.");
    } else {
        note = tr("Off: values are those Codex recorded the last time you used it. Turning this on asks Codex "
                  "(codex app-server) every 5 minutes; Codex contacts OpenAI with its own login.");
    }
    m_codexLiveNote->setText(note);
    updateIntegrationAvailability();
}

QStringList SettingsWindow::claudeEnableArguments() const
{
    QStringList arguments{QStringLiteral("enable")};
    if (m_claudeContext->isChecked()) {
        arguments << QStringLiteral("--context");
        const QString cwd = m_claudeCwd->text().trimmed();
        if (!cwd.isEmpty())
            arguments << QStringLiteral("--cwd") << cwd;
    }
    return arguments;
}

QString SettingsWindow::integrationMetricFile() const
{
    if (!m_claude || !m_claude->status().value(QStringLiteral("enabled")).toBool())
        return {};
    return QDir::cleanPath(m_claude->status().value(QStringLiteral("metricFile")).toString());
}

void SettingsWindow::syncClaude()
{
    if (!m_claude)
        return;
    const QJsonObject &status = m_claude->status();
    const bool known = m_claude->statusKnown();
    const bool enabled = status.value(QStringLiteral("enabled")).toBool();
    const bool userPackage = status.value(QStringLiteral("userPackage")).toBool();
    const QString live = status.value(QStringLiteral("liveUpdates")).toString();
    const bool context = status.value(QStringLiteral("context")).toBool();
    {
        const QSignalBlocker blockEnabled(m_claudeEnabled);
        const QSignalBlocker blockLive(m_claudeLive);
        const QSignalBlocker blockContext(m_claudeContext);
        m_claudeEnabled->setChecked(enabled);
        m_claudeLive->setChecked(live == QLatin1String("on"));
        m_claudeContext->setChecked(context);
        if (!m_claudeCwd->hasFocus())
            m_claudeCwd->setText(status.value(QStringLiteral("cwd")).toString());
    }
    const bool idle = m_claude->installed() && known && !m_claude->busy();
    // A package enabled before the provenance field existed is rewritten
    // once, with its own options, so that it is shown under Integrations.
    if (idle && enabled && !status.value(QStringLiteral("integrationMarked")).toBool() && !m_claudeMigrated) {
        m_claudeMigrated = true;
        m_claude->run(claudeEnableArguments());
    }
    m_claudeEnabled->setEnabled(idle && !userPackage);
    m_claudeLive->setEnabled(idle && enabled && status.value(QStringLiteral("settingsError")).toString().isEmpty());
    m_claudeContext->setEnabled(idle && enabled);
    m_claudeCwd->setEnabled(idle && enabled && context);
    m_claudeBrowse->setEnabled(idle && enabled && context);

    QString note;
    if (live == QLatin1String("on")) {
        note = status.value(QStringLiteral("chainedStatusLine")).toBool()
            ? tr("Usage is reported after every Claude response; your own status line still runs after AQTRunCat's.")
            : tr("Usage is reported after every Claude response.");
        const QDateTime last = QDateTime::fromString(status.value(QStringLiteral("lastLiveReport")).toString(), Qt::ISODate);
        note += u' ' + (last.isValid() ? tr("Last report: %1.").arg(QLocale().toString(last.toLocalTime(), QLocale::ShortFormat))
                                       : tr("No report yet: it arrives with the next Claude response."));
    } else if (live == QLatin1String("replaced")) {
        note = tr("Off: Claude Code's statusLine was changed after live updates were turned on, so it was left alone.");
    } else {
        note = tr("Off: values update when you open /usage in Claude Code. Turning this on sets \"statusLine\" in %1.")
                   .arg(status.value(QStringLiteral("settingsFile")).toString());
        if (status.value(QStringLiteral("otherStatusLine")).toBool())
            note += u' ' + tr("Your current status line would keep working.");
    }
    if (const QString error = status.value(QStringLiteral("settingsError")).toString(); !error.isEmpty())
        note += u' ' + tr("Cannot use it: %1").arg(error);
    m_claudeLiveNote->setText(note);
    updateIntegrationAvailability();
}

QString SettingsWindow::integrationAvailability(const ProviderIntegration *integration, const QString &adapter,
                                               const QString &provider, const QString &foundKey) const
{
    const QJsonObject &status = integration->status();
    if (!integration->installed())
        return tr("Not available: %1 is not installed.").arg(adapter);
    if (!integration->statusKnown())
        return integration->lastError().isEmpty() ? tr("Checking…") : tr("Error: %1").arg(integration->lastError());
    if (status.value(QStringLiteral("userPackage")).toBool()) {
        return tr("A user-defined Custom Metric already uses the id \"%1\" (%2); AQTRunCat does not replace it.")
            .arg(integration->id(), status.value(QStringLiteral("metricFile")).toString());
    }
    if (!status.value(QStringLiteral("enabled")).toBool())
        return status.value(foundKey).toBool() ? tr("Off.") : tr("Off. %1 was not found on this system.").arg(provider);
    // The integration's own source in the shared F1 service.
    const QString file = QDir::cleanPath(status.value(QStringLiteral("metricFile")).toString());
    QString text = tr("On — waiting for the first update…");
    if (!m_customMetrics)
        return text;
    for (const aqtruncat::custommetrics::Source &source : m_customMetrics->sources()) {
        if (QDir::cleanPath(source.path) != file)
            continue;
        switch (source.state) {
        case aqtruncat::custommetrics::State::Available: text = tr("On — available."); break;
        case aqtruncat::custommetrics::State::Pending:
            if (source.snapshot && !source.snapshot->metrics.isEmpty())
                text = tr("On — available.");
            break;
        case aqtruncat::custommetrics::State::Unavailable: text = tr("On — unavailable: %1").arg(source.message); break;
        case aqtruncat::custommetrics::State::Error: text = tr("On — error: %1").arg(source.message); break;
        }
    }
    return text;
}

void SettingsWindow::updateIntegrationAvailability()
{
    if (m_claude)
        m_claudeStatus->setText(integrationAvailability(m_claude, QStringLiteral("aqtruncat-claude-metric"),
                                                        tr("Claude Code"), QStringLiteral("claudeFound")));
    if (m_codex)
        m_codexStatus->setText(integrationAvailability(m_codex, QStringLiteral("aqtruncat-codex-metric"),
                                                       tr("Codex"), QStringLiteral("codexFound")));
}

bool SettingsWindow::addCustomMetricFile(const QString &path, QString *error)
{
    return m_customMetrics && m_customMetrics->registerFile(path, error);
}

void SettingsWindow::showAndRaise()
{
    if (m_claude)
        m_claude->refresh();
    show();
    raise();
    activateWindow();
}

void SettingsWindow::setActiveRunner(const aqtruncat::Runner &runner)
{
    m_activeRunnerId = runner.id;
    m_preview->setRunner(runner);
    const int index = m_runnerCombo->findData(runner.id);
    if (index >= 0) {
        const QSignalBlocker blocker(m_runnerCombo);
        m_runnerCombo->setCurrentIndex(index);
    }
}

void SettingsWindow::setAvailableInterfaces(const std::vector<aqtruncat::metrics::InterfaceMetrics> &interfaces)
{
    bool changed = interfaces.size() != m_availableInterfaces.size();
    if (!changed) {
        for (std::size_t i = 0; i < interfaces.size(); ++i)
            if (interfaces[i].name != m_availableInterfaces[i].name || interfaces[i].kind != m_availableInterfaces[i].kind)
                changed = true;
    }
    if (!changed)
        return;
    m_availableInterfaces = interfaces;
    rebuildInterfaces();
}

void SettingsWindow::syncFromSettings()
{
    m_rebuilding = true;
    const int runner = m_runnerCombo->findData(m_activeRunnerId.isEmpty() ? m_settings->runnerId() : m_activeRunnerId);
    if (runner >= 0)
        m_runnerCombo->setCurrentIndex(runner);
    m_normal->setChecked(m_settings->speedMode() == aqtruncat::SpeedMode::Normal);
    m_slow->setChecked(m_settings->speedMode() == aqtruncat::SpeedMode::SlowDown);
    m_networkAutomatic->setChecked(m_settings->networkMode() == NetworkPolicyMode::Automatic);
    m_networkManual->setChecked(m_settings->networkMode() == NetworkPolicyMode::Manual);
    m_interfaces->setEnabled(m_settings->networkMode() == NetworkPolicyMode::Manual);
    const int refresh = m_refresh->findData(m_settings->dashboardRefreshSeconds());
    m_refresh->setCurrentIndex(refresh >= 0 ? refresh : 0);
    m_autostart->setChecked(m_settings->autostartEnabled());
    m_fpsSource->setCurrentIndex(std::max(0, m_fpsSource->findData(m_settings->fpsSource())));
    m_showCpuTemperature->setChecked(m_settings->showCpuTemperature());
    m_showGpuMemory->setChecked(m_settings->showGpuMemory());
    m_showGpuTemperature->setChecked(m_settings->showGpuTemperature());
    m_showFps->setChecked(m_settings->showFps());
    const int interval = m_updatesInterval->findData(m_settings->updatesIntervalMinutes());
    m_updatesInterval->setCurrentIndex(interval >= 0 ? interval : 0);
    m_updatesRemote->setChecked(m_settings->updatesRemoteRefresh());
    m_updatesRemote->setEnabled(m_settings->updatesIntervalMinutes() > 0);
    m_rebuilding = false;
    rebuildInterfaces();
    rebuildGpus();
}

void SettingsWindow::setAvailableGpus(const std::vector<aqtruncat::metrics::GpuDevice> &gpus)
{
    std::vector<std::pair<std::string, std::string>> available;
    for (const auto &gpu : gpus)
        available.emplace_back(gpu.id, gpu.model);
    if (available == m_availableGpus)
        return;
    m_availableGpus = std::move(available);
    rebuildGpus();
}

void SettingsWindow::rebuildGpus()
{
    m_rebuilding = true;
    m_gpuDevice->clear();
    m_gpuDevice->addItem(tr("Automatic (discrete GPU first)"), QStringLiteral("auto"));
    for (const auto &[id, model] : m_availableGpus)
        m_gpuDevice->addItem(QStringLiteral("%1 (%2)").arg(QString::fromStdString(model), QString::fromStdString(id)),
                             QString::fromStdString(id));
    // A chosen GPU that is not present now (eGPU, other machine) stays chosen.
    const QString chosen = m_settings->gpuDevice();
    if (m_gpuDevice->findData(chosen) < 0)
        m_gpuDevice->addItem(tr("%1 (not present)").arg(chosen), chosen);
    m_gpuDevice->setCurrentIndex(m_gpuDevice->findData(chosen));
    m_rebuilding = false;
}

void SettingsWindow::rebuildInterfaces()
{
    m_rebuilding = true;
    m_interfaces->clear();
    std::set<std::string> available;
    for (const auto &interface : m_availableInterfaces) {
        available.insert(interface.name);
        auto *item = new QTreeWidgetItem(m_interfaces);
        item->setText(0, QString::fromStdString(interface.name));
        item->setText(1, kindName(interface.kind));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, m_settings->networkInterfaces().contains(item->text(0)) ? Qt::Checked : Qt::Unchecked);
    }
    for (const QString &selected : m_settings->networkInterfaces()) {
        if (available.contains(selected.toStdString()))
            continue;
        auto *item = new QTreeWidgetItem(m_interfaces);
        item->setText(0, selected);
        item->setText(1, tr("Not currently available"));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
    }
    m_interfaces->resizeColumnToContents(0);
    m_rebuilding = false;
}

void SettingsWindow::storeCheckedInterfaces()
{
    if (m_rebuilding)
        return;
    QStringList selected;
    for (int i = 0; i < m_interfaces->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_interfaces->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked)
            selected.append(item->text(0));
    }
    m_settings->setNetworkInterfaces(std::move(selected));
}

void SettingsWindow::rebuildCustomMetrics()
{
    // Command sources republish on every run: keep the user's selection.
    const QString current = m_customMetricFiles->currentItem()
        ? m_customMetricFiles->currentItem()->text(1) : QString();
    m_rebuildingCustomMetrics = true; // setCheckState below emits itemChanged
    m_customMetricFiles->clear();
    m_removeCustomMetric->setEnabled(false);
    if (!m_customMetrics) {
        m_rebuildingCustomMetrics = false;
        return;
    }
    QTreeWidgetItem *restored = nullptr;
    const QString integrationFile = integrationMetricFile();
    QHash<QString, QTreeWidgetItem *> groups; // v2F1.5: children under their group
    for (const aqtruncat::custommetrics::Source &source : m_customMetrics->sources()) {
        // Managed in Integrations, not one of the user's Custom Metrics. The
        // path check covers a package written before the provenance field.
        if (!source.integration.isEmpty()
            || (!integrationFile.isEmpty() && QDir::cleanPath(source.path) == integrationFile))
            continue;
        QTreeWidgetItem *parent = groups.value(source.parentId);
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_customMetricFiles);
        if (source.type == aqtruncat::custommetrics::SourceType::Group)
            groups.insert(source.id, item);
        if (!current.isEmpty() && source.path == current)
            restored = item;
        // The metric's own title first (a disabled one keeps it too); the
        // file name only while the file has none (unreadable, invalid).
        item->setText(0, source.snapshot && !source.snapshot->title.isEmpty() ? source.snapshot->title
                                                                             : QFileInfo(source.path).completeBaseName());
        item->setText(1, source.path);
        item->setToolTip(1, source.path);
        item->setData(0, Qt::UserRole, source.explicitPath.isEmpty() ? source.path : source.explicitPath);
        item->setData(0, Qt::UserRole + 1, source.explicitlyRegistered);
        // v2F1.2: the checkbox is the file's own "enabled" (missing = on);
        // v2F1.5: under a disabled group a checked child stays inactive.
        item->setData(0, Qt::UserRole + 2, source.configuredEnabled);
        item->setFlags((item->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsAutoTristate);
        item->setCheckState(0, source.configuredEnabled ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(0, source.enabled ? tr("On: the metric runs and is shown. Uncheck to disable it.")
                                           : tr("Off: the metric does not run and is not shown. Check to enable it."));
        QString origin;
        if (source.explicitlyRegistered && source.discovered)
            origin = tr("Registered + discovered");
        else if (source.explicitlyRegistered)
            origin = tr("Registered");
        else
            origin = tr("Discovered");
        if (!source.parentId.isEmpty())
            origin = source.type == aqtruncat::custommetrics::SourceType::Group ? tr("Group in a group") : tr("Group child");
        else if (source.type == aqtruncat::custommetrics::SourceType::Group)
            origin = tr("Group — %1").arg(origin);
        else if (source.type == aqtruncat::custommetrics::SourceType::Command)
            origin = tr("Command — %1").arg(origin);
        item->setText(2, origin);
        switch (source.state) {
        case aqtruncat::custommetrics::State::Available: item->setText(3, tr("Available")); break;
        case aqtruncat::custommetrics::State::Pending: item->setText(3, tr("Waiting…")); break;
        case aqtruncat::custommetrics::State::Unavailable: item->setText(3, tr("Unavailable")); break;
        case aqtruncat::custommetrics::State::Error: item->setText(3, tr("Error")); break;
        }
        if (source.type == aqtruncat::custommetrics::SourceType::Group && source.state != aqtruncat::custommetrics::State::Error)
            item->setText(3, source.enabled ? tr("Group") : tr("Disabled"));
        if (!source.enabled && source.configuredEnabled) {
            item->setText(3, tr("Disabled by group"));
            for (int column = 0; column < 4; ++column)
                item->setForeground(column, palette().brush(QPalette::Disabled, QPalette::Text));
        } else if (!source.enabled)
            item->setText(3, tr("Disabled"));
        else if (source.state == aqtruncat::custommetrics::State::Error)
            item->setIcon(3, QIcon::fromTheme(QStringLiteral("dialog-error"),
                                              style()->standardIcon(QStyle::SP_MessageBoxCritical)));
        // v2F1.6: the warnings (the metric still works) under its message:
        // its own, then its values outside their range.
        QStringList warnings = source.warnings;
        if (source.snapshot && source.enabled && source.state != aqtruncat::custommetrics::State::Error
            && source.state != aqtruncat::custommetrics::State::Unavailable)
            warnings += aqtruncat::custommetrics::valueWarnings(*source.snapshot);
        if (source.state != aqtruncat::custommetrics::State::Error && source.enabled && !warnings.isEmpty())
            item->setIcon(3, QIcon::fromTheme(QStringLiteral("dialog-warning"),
                                              style()->standardIcon(QStyle::SP_MessageBoxWarning)));
        QStringList status;
        if (!source.message.isEmpty())
            status.append(source.message);
        for (const QString &warning : std::as_const(warnings))
            status.append(tr("Warning: %1").arg(warning));
        item->setToolTip(3, status.join(u'\n'));
    }
    m_customMetricFiles->setRootIsDecorated(!groups.isEmpty());
    m_customMetricFiles->expandAll();
    for (int column = 0; column < 4; ++column)
        m_customMetricFiles->resizeColumnToContents(column);
    if (restored)
        m_customMetricFiles->setCurrentItem(restored);
    m_rebuildingCustomMetrics = false;
}

void SettingsWindow::storeCustomMetricEnabled(QTreeWidgetItem *item)
{
    if (m_rebuildingCustomMetrics || !m_customMetrics || !item)
        return;
    const bool wanted = item->checkState(0) == Qt::Checked;
    if (wanted == item->data(0, Qt::UserRole + 2).toBool())
        return;
    item->setData(0, Qt::UserRole + 2, wanted);
    // Applied after this itemChanged: the engine republishes its sources and
    // the list is rebuilt, which must not delete the emitting item.
    const QString path = item->data(0, Qt::UserRole).toString();
    QMetaObject::invokeMethod(this, [this, path, wanted] { applyCustomMetricEnabled(path, wanted); },
                              Qt::QueuedConnection);
}

void SettingsWindow::applyCustomMetricEnabled(const QString &path, bool enabled)
{
    QString error;
    if (m_customMetrics && m_customMetrics->setEnabled(path, enabled, &error))
        return; // the republished sources update the row
    QMessageBox::warning(this, enabled ? tr("Cannot Enable Custom Metric") : tr("Cannot Disable Custom Metric"),
                         error.isEmpty() ? tr("The file could not be changed.") : error);
    rebuildCustomMetrics(); // back to the actual state
}

void SettingsWindow::rebuildMetricsBar()
{
    using namespace aqtruncat::metricsbar;
    const QString current = m_metricsBarItems->currentItem()
        ? m_metricsBarItems->currentItem()->data(0, Qt::UserRole).toString() : QString();
    m_rebuilding = true;
    m_metricsBarEnabled->setChecked(m_settings->metricsBarEnabled());
    m_metricsBarItems->clear();

    const QList<aqtruncat::custommetrics::Source> sources =
        m_customMetrics ? m_customMetrics->sources() : QList<aqtruncat::custommetrics::Source>();
    // The limit is enforced by AppSettings and the model; here it is made
    // visible: once it is reached, unselected rows cannot be checked.
    const QStringList selected = m_settings->metricsBarItems();
    const bool full = selected.size() >= MaxItems;
    auto addRow = [this, full](const QString &id, const QString &name, const QString &note, bool checked) {
        auto *item = new QTreeWidgetItem(m_metricsBarItems);
        item->setText(0, name);
        item->setText(1, note);
        item->setData(0, Qt::UserRole, id);
        Qt::ItemFlags flags = (item->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsAutoTristate;
        if (full && !checked)
            flags &= ~Qt::ItemIsEnabled;
        item->setFlags(flags);
        item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
    };
    auto addKnown = [&](const QString &id, bool checked) {
        for (const BuiltInMetric &metric : builtInMetrics()) {
            if (metric.id != id)
                continue;
            const QString note = id == QLatin1String("storage.home")
                ? tr("Only when Home is a separate filesystem") : QString();
            addRow(id, metric.label, note, checked);
            return true;
        }
        if (isCustomItemId(id)) {
            const QString sourceId = sourceIdFromCustomItemId(id);
            const QString itemId = itemIdFromCustomItemId(id);
            const auto source = std::find_if(sources.cbegin(), sources.cend(),
                                             [&](const auto &s) { return s.id == sourceId; });
            if (source != sources.cend() && !source->enabled) {
                // v2F1.2: kept selected, shown again once enabled.
                addRow(id, itemId, tr("Custom — disabled"), checked);
                return true;
            }
            const aqtruncat::custommetrics::Snapshot *snapshot =
                source != sources.cend() && source->snapshot ? &*source->snapshot : nullptr;
            const aqtruncat::custommetrics::Item *metric = nullptr;
            if (snapshot) {
                for (const auto &candidate : snapshot->metrics) {
                    if (candidate.id == itemId)
                        metric = &candidate;
                }
            }
            if (metric)
                addRow(id, snapshot->title + QStringLiteral(" — ") + metric->title,
                       source->integration.isEmpty() ? tr("Custom") : tr("Integration"), checked);
            else
                addRow(id, snapshot ? snapshot->title + QStringLiteral(" — ") + itemId : itemId,
                       tr("Custom — not currently available"), checked);
            return true;
        }
        if (!isCustomId(id))
            return false;
        const QString sourceId = sourceIdFromCustomId(id);
        const auto source = std::find_if(sources.cbegin(), sources.cend(),
                                         [&](const auto &s) { return s.id == sourceId; });
        if (source == sources.cend()) {
            addRow(id, sourceId, tr("Custom — not currently available"), checked);
            return true;
        }
        const auto &snapshot = source->snapshot;
        QString name = snapshot ? snapshot->title : QFileInfo(source->path).completeBaseName();
        if (!source->parentId.isEmpty()) {
            // v2F1.5: a group's child, named with its group.
            const auto group = std::find_if(sources.cbegin(), sources.cend(),
                                            [&](const auto &s) { return s.id == source->parentId; });
            if (group != sources.cend() && group->snapshot)
                name = group->snapshot->title + QStringLiteral(" — ") + name;
        }
        if (!source->enabled) {
            addRow(id, name, tr("Custom — disabled"), checked);
            return true;
        }
        const bool noValue = source->type == aqtruncat::custommetrics::SourceType::Snapshot && snapshot
            && (!snapshot->metricsBarValue || snapshot->metricsBarValue->trimmed().isEmpty());
        const QString kind = source->integration.isEmpty() ? tr("Custom") : tr("Integration");
        addRow(id, name, noValue ? tr("%1 — provides no Metrics Bar value").arg(kind) : kind, checked);
        return true;
    };

    // Selected entries first, in the user's order; then everything else.
    for (const QString &id : selected)
        addKnown(id, true);
    for (const BuiltInMetric &metric : builtInMetrics()) {
        if (!selected.contains(metric.id))
            addKnown(metric.id, false);
    }
    for (const auto &source : sources) {
        if (!source.enabled || source.type == aqtruncat::custommetrics::SourceType::Group)
            continue; // not offered while disabled (a selection stays, above); groups are containers
        // A multi-value source whose items carry their own compact values is
        // offered per item; the whole-source entry stays only if selected.
        QStringList itemIds;
        if (source.snapshot) {
            for (const auto &metric : source.snapshot->metrics) {
                if (isSelectableItem(metric))
                    itemIds.append(customItemId(source.id, metric.id));
            }
        }
        const QString id = customId(source.id);
        if (!selected.contains(id) && itemIds.isEmpty())
            addKnown(id, false);
        for (const QString &itemId : std::as_const(itemIds)) {
            if (!selected.contains(itemId))
                addKnown(itemId, false);
        }
    }
    m_metricsBarItems->resizeColumnToContents(0);
    for (int i = 0; i < m_metricsBarItems->topLevelItemCount(); ++i) {
        if (m_metricsBarItems->topLevelItem(i)->data(0, Qt::UserRole).toString() == current)
            m_metricsBarItems->setCurrentItem(m_metricsBarItems->topLevelItem(i));
    }
    m_metricsBarCount->setText(tr("%1 of %2 metrics selected. Each one is a separate tray icon next to "
                                  "the runner; hover an icon for its full value.")
                                   .arg(selected.size()).arg(MaxItems));
    m_rebuilding = false;
}

void SettingsWindow::storeMetricsBarItems()
{
    if (m_rebuilding)
        return;
    QStringList ids;
    for (int i = 0; i < m_metricsBarItems->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *item = m_metricsBarItems->topLevelItem(i);
        if (item->checkState(0) == Qt::Checked)
            ids.append(item->data(0, Qt::UserRole).toString());
    }
    m_settings->setMetricsBarItems(ids);
    // AppSettings may refuse part of the change (limit reached) without
    // emitting anything: always show its actual state afterwards.
    QMetaObject::invokeMethod(this, [this] { rebuildMetricsBar(); }, Qt::QueuedConnection);
}

void SettingsWindow::moveMetricsBarItem(int delta)
{
    QTreeWidgetItem *item = m_metricsBarItems->currentItem();
    if (!item)
        return;
    const int from = m_metricsBarItems->indexOfTopLevelItem(item);
    const int to = from + delta;
    if (to < 0 || to >= m_metricsBarItems->topLevelItemCount())
        return;
    m_rebuilding = true;
    m_metricsBarItems->takeTopLevelItem(from);
    m_metricsBarItems->insertTopLevelItem(to, item);
    m_metricsBarItems->setCurrentItem(item);
    m_rebuilding = false;
    storeMetricsBarItems();
}

void SettingsWindow::closeEvent(QCloseEvent *event)
{
    if (m_closeTerminatesApplication) {
        event->accept();
        QCoreApplication::quit();
        return;
    }
    hide();
    event->ignore();
}
