#include "settingswindow.h"

#include "runnerpreviewwidget.h"
#include "custommetricsservice.h"
#include "metricsbarmodel.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
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
                               CustomMetricsService *customMetrics, QWidget *parent)
    : QWidget(parent, Qt::Tool)
    , m_settings(settings)
    , m_customMetrics(customMetrics)
    , m_runners(std::move(runners))
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
    m_customMetricFiles->setHeaderLabels({tr("File"), tr("Origin"), tr("Status")});
    m_customMetricFiles->setRootIsDecorated(false);
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
    customButtons->addWidget(addCustomMetric);
    customButtons->addWidget(m_removeCustomMetric);
    customButtons->addStretch();
    customLayout->addLayout(customButtons);
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
    m_autostart = new QCheckBox(tr("Start AQTRunCat automatically on login"), desktopBox);
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
    connect(m_customMetricFiles, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        m_removeCustomMetric->setEnabled(item && item->data(0, Qt::UserRole + 1).toBool());
    });
    connect(m_removeCustomMetric, &QPushButton::clicked, this, [this] {
        if (!m_customMetrics)
            return;
        if (QTreeWidgetItem *item = m_customMetricFiles->currentItem())
            m_customMetrics->unregisterFile(item->data(0, Qt::UserRole).toString());
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
        connect(m_customMetrics, &CustomMetricsService::sourcesChanged, this, [this] {
            rebuildCustomMetrics();
            rebuildMetricsBar();
        });
    }
    syncFromSettings();
    rebuildCustomMetrics();
    rebuildMetricsBar();
}

bool SettingsWindow::addCustomMetricFile(const QString &path, QString *error)
{
    return m_customMetrics && m_customMetrics->registerFile(path, error);
}

void SettingsWindow::showAndRaise()
{
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
    m_rebuilding = false;
    rebuildInterfaces();
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
    m_customMetricFiles->clear();
    if (!m_customMetrics)
        return;
    for (const aqtruncat::custommetrics::Source &source : m_customMetrics->sources()) {
        auto *item = new QTreeWidgetItem(m_customMetricFiles);
        item->setText(0, source.path);
        item->setData(0, Qt::UserRole, source.explicitPath.isEmpty() ? source.path : source.explicitPath);
        item->setData(0, Qt::UserRole + 1, source.explicitlyRegistered);
        if (source.explicitlyRegistered && source.discovered)
            item->setText(1, tr("Registered + discovered"));
        else if (source.explicitlyRegistered)
            item->setText(1, tr("Registered"));
        else
            item->setText(1, tr("Discovered"));
        switch (source.state) {
        case aqtruncat::custommetrics::State::Available: item->setText(2, tr("Available")); break;
        case aqtruncat::custommetrics::State::Pending: item->setText(2, tr("Waiting…")); break;
        case aqtruncat::custommetrics::State::Unavailable: item->setText(2, tr("Unavailable")); break;
        case aqtruncat::custommetrics::State::Error: item->setText(2, tr("Error")); break;
        }
        item->setToolTip(2, source.message);
    }
    for (int column = 0; column < 3; ++column)
        m_customMetricFiles->resizeColumnToContents(column);
    m_removeCustomMetric->setEnabled(false);
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
        const QString name = snapshot ? snapshot->title : QFileInfo(source->path).completeBaseName();
        const bool noValue = snapshot && (!snapshot->metricsBarValue || snapshot->metricsBarValue->trimmed().isEmpty());
        addRow(id, name, noValue ? tr("Custom — provides no Metrics Bar value") : tr("Custom"), checked);
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
        const QString id = customId(source.id);
        if (!selected.contains(id))
            addKnown(id, false);
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
    hide();
    event->ignore();
}
