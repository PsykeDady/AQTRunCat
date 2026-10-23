#include "dashboardwindow.h"

#include "customsymbol.h"
#include "metrics/metricsformat.h"
#include "metrics/uptime.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QFont>
#include <QGridLayout>
#include <QHideEvent>
#include <QShowEvent>
#include <QStyle>
#include <QScrollArea>
#include <QScrollBar>
#include <QFileInfo>
#include <QLocale>

#include <algorithm>

namespace {

using namespace aqtruncat::metrics;

QString q(const std::string &text)
{
    return QString::fromStdString(text);
}

QString bytes(std::uint64_t value)
{
    return q(formatBytes(value));
}

QString rate(const std::optional<double> &value)
{
    return value ? q(formatRate(*value)) : QStringLiteral("N/A");
}

QString percent(double value)
{
    return q(formatPercent(value));
}

QLabel *label(const QString &text, QWidget *parent, const char *name = nullptr)
{
    auto *result = new QLabel(text, parent);
    if (name)
        result->setObjectName(QLatin1String(name));
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return result;
}

QWidget *twoValues(const QString &leftTitle, QLabel **leftValue, const char *leftName,
                   const QString &rightTitle, QLabel **rightValue, const char *rightName,
                   QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *grid = new QGridLayout(row);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(18);
    grid->addWidget(label(leftTitle, row), 0, 0);
    grid->addWidget(label(rightTitle, row), 0, 1);
    *leftValue = label(QStringLiteral("N/A"), row, leftName);
    *rightValue = label(QStringLiteral("N/A"), row, rightName);
    QFont valueFont = (*leftValue)->font();
    valueFont.setBold(true);
    (*leftValue)->setFont(valueFont);
    (*rightValue)->setFont(valueFont);
    grid->addWidget(*leftValue, 1, 0);
    grid->addWidget(*rightValue, 1, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    return row;
}

} // namespace

DashboardWindow::DashboardWindow(std::string homePath, QWidget *parent)
    : QWidget(parent, Qt::Tool)
    , m_homePath(std::move(homePath))
{
    setObjectName(QStringLiteral("dashboardWindow"));
    setWindowTitle(tr("AQTRunCat Dashboard"));
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumWidth(340);
    resize(380, 500);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("dashboardScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    auto *contents = new QWidget(m_scroll);
    m_layout = new QVBoxLayout(contents);
    m_layout->setContentsMargins(16, 14, 16, 14);
    m_layout->setSpacing(8);
    m_scroll->setWidget(contents);
    outer->addWidget(m_scroll);

    auto *title = label(QStringLiteral("AQTRunCat"), this, "dashboardTitle");
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.25);
    titleFont.setBold(true);
    title->setFont(titleFont);
    m_layout->addWidget(title);

    m_layout->addWidget(sectionHeader(tr("CPU"), this));
    auto *cpuTop = new QWidget(this);
    auto *cpuTopLayout = new QHBoxLayout(cpuTop);
    cpuTopLayout->setContentsMargins(0, 0, 0, 0);
    m_cpuDetail = label(QString(), cpuTop, "cpuDetail");
    m_cpuValue = label(QStringLiteral("N/A"), cpuTop, "cpuValue");
    QFont primary = m_cpuValue->font();
    primary.setBold(true);
    m_cpuValue->setFont(primary);
    cpuTopLayout->addWidget(m_cpuDetail);
    cpuTopLayout->addStretch();
    cpuTopLayout->addWidget(m_cpuValue);
    m_layout->addWidget(cpuTop);
    m_cpuBar = new QProgressBar(this);
    m_cpuBar->setObjectName(QStringLiteral("cpuProgress"));
    m_cpuBar->setAccessibleName(tr("CPU usage"));
    m_cpuBar->setTextVisible(false);
    setProgress(m_cpuBar, std::nullopt);
    m_layout->addWidget(m_cpuBar);

    m_layout->addWidget(sectionHeader(tr("Memory"), this));
    auto *memoryTop = new QWidget(this);
    auto *memoryTopLayout = new QHBoxLayout(memoryTop);
    memoryTopLayout->setContentsMargins(0, 0, 0, 0);
    m_memoryDetail = label(QStringLiteral("N/A"), memoryTop, "memoryDetail");
    m_memoryValue = label(QStringLiteral("N/A"), memoryTop, "memoryValue");
    m_memoryValue->setFont(primary);
    memoryTopLayout->addWidget(m_memoryDetail);
    memoryTopLayout->addStretch();
    memoryTopLayout->addWidget(m_memoryValue);
    m_layout->addWidget(memoryTop);
    m_memoryBar = new QProgressBar(this);
    m_memoryBar->setObjectName(QStringLiteral("memoryProgress"));
    m_memoryBar->setAccessibleName(tr("Memory usage"));
    m_memoryBar->setTextVisible(false);
    setProgress(m_memoryBar, std::nullopt);
    m_layout->addWidget(m_memoryBar);
    auto *swapRow = new QWidget(this);
    auto *swapLayout = new QHBoxLayout(swapRow);
    swapLayout->setContentsMargins(0, 0, 0, 0);
    swapLayout->addWidget(label(tr("Swap"), swapRow));
    swapLayout->addStretch();
    m_swapValue = label(QStringLiteral("N/A"), swapRow, "swapValue");
    swapLayout->addWidget(m_swapValue);
    m_layout->addWidget(swapRow);

    m_layout->addWidget(sectionHeader(tr("Network"), this));
    m_layout->addWidget(twoValues(tr("Download"), &m_networkDown, "networkDownload",
                                  tr("Upload"), &m_networkUp, "networkUpload", this));

    m_layout->addWidget(sectionHeader(tr("Storage"), this));
    auto *storage = new QWidget(this);
    m_storageLayout = new QVBoxLayout(storage);
    m_storageLayout->setContentsMargins(0, 0, 0, 0);
    m_storageLayout->setSpacing(6);
    m_layout->addWidget(storage);

    m_layout->addWidget(sectionHeader(tr("Disk I/O"), this));
    m_layout->addWidget(twoValues(tr("Read"), &m_diskRead, "diskRead",
                                  tr("Write"), &m_diskWrite, "diskWrite", this));

    // F5 built-in metrics.
    m_layout->addWidget(sectionHeader(tr("GPU"), this));
    auto *gpus = new QWidget(this);
    gpus->setObjectName(QStringLiteral("gpuContainer"));
    m_gpuLayout = new QVBoxLayout(gpus);
    m_gpuLayout->setContentsMargins(0, 0, 0, 0);
    m_gpuLayout->setSpacing(8);
    m_gpuStatus = label(tr("Waiting…"), gpus, "gpuStatus");
    m_gpuLayout->addWidget(m_gpuStatus);
    m_layout->addWidget(gpus);

    m_gamingHeader = sectionHeader(tr("Gaming"), this);
    m_gamingHeader->setObjectName(QStringLiteral("gamingHeader"));
    m_layout->addWidget(m_gamingHeader);
    m_fps = textRow(tr("FPS"), "fps", this);

    m_layout->addWidget(sectionHeader(tr("Updates"), this));
    m_packageUpdates = textRow(tr("Packages"), "packageUpdates", this);
    m_flatpakUpdates = textRow(tr("Flatpak"), "flatpakUpdates", this);

    m_layout->addWidget(sectionHeader(tr("System"), this));
    m_uptime = textRow(tr("Uptime"), "uptime", this);
    m_kernel = textRow(tr("Kernel"), "kernel", this);
    m_distribution = textRow(tr("Distribution"), "distribution", this);

    auto section = [this](const QString &title, const char *name, QWidget *&header, QWidget *&container,
                          QVBoxLayout *&layout) {
        header = sectionHeader(title, this);
        header->setObjectName(QStringLiteral("%1Header").arg(QLatin1String(name)));
        m_layout->addWidget(header);
        container = new QWidget(this);
        container->setObjectName(QStringLiteral("%1Container").arg(QLatin1String(name)));
        layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        m_layout->addWidget(container);
        header->hide(); // until there is something to show
        container->hide();
    };
    section(tr("Integrations"), "integrations", m_integrationsHeader, m_integrationsContainer, m_integrationsLayout);
    section(tr("Custom Metrics"), "customMetrics", m_customHeader, m_customContainer, m_customLayout);
    m_layout->addStretch();
}

DashboardWindow::TextRow DashboardWindow::textRow(const QString &title, const char *name, QWidget *parent)
{
    auto *container = new QWidget(parent);
    auto *grid = new QGridLayout(container);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(0);
    TextRow row;
    row.container = container;
    container->setObjectName(QStringLiteral("%1Row").arg(QLatin1String(name)));
    row.warning = new QLabel(container);
    row.warning->setObjectName(QStringLiteral("%1Warning").arg(QLatin1String(name)));
    const QIcon warningIcon = QIcon::fromTheme(QStringLiteral("dialog-warning"),
                                               style()->standardIcon(QStyle::SP_MessageBoxWarning));
    row.warning->setPixmap(warningIcon.pixmap(16, 16));
    row.warning->setAccessibleName(tr("Warning"));
    row.warning->hide();
    row.value = label(QStringLiteral("N/A"), container, nullptr);
    row.value->setObjectName(QStringLiteral("%1Value").arg(QLatin1String(name)));
    row.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    QFont bold = row.value->font();
    bold.setBold(true);
    row.value->setFont(bold);
    row.detail = label(QString(), container, nullptr);
    row.detail->setObjectName(QStringLiteral("%1Detail").arg(QLatin1String(name)));
    row.detail->setWordWrap(true);
    QFont secondary = row.detail->font();
    secondary.setPointSizeF(secondary.pointSizeF() * 0.9);
    row.detail->setFont(secondary);
    row.detail->hide();
    grid->addWidget(row.warning, 0, 0);
    grid->addWidget(label(title, container), 0, 1);
    grid->addWidget(row.value, 0, 2);
    grid->addWidget(row.detail, 1, 1, 1, 2);
    grid->setColumnStretch(1, 1);
    m_layout->addWidget(container);
    return row;
}

void DashboardWindow::setText(TextRow &row, const QString &value, const QString &detail, bool warning, const QString &tooltip)
{
    row.value->setText(value);
    row.value->setToolTip(tooltip);
    row.detail->setText(detail);
    row.detail->setVisible(!detail.isEmpty());
    row.warning->setVisible(warning);
    row.warning->setToolTip(warning ? detail : QString());
}

QWidget *DashboardWindow::sectionHeader(const QString &title, QWidget *parent)
{
    auto *header = label(title, parent);
    QFont font = header->font();
    font.setBold(true);
    header->setFont(font);
    header->setContentsMargins(0, 5, 0, 0);
    return header;
}

void DashboardWindow::setProgress(QProgressBar *bar, const std::optional<double> &value,
                                  const QString &unavailable)
{
    bar->setRange(0, 1000);
    if (!value) {
        bar->setValue(0);
        bar->setFormat(unavailable);
        bar->setAccessibleDescription(unavailable);
        return;
    }
    const double bounded = std::clamp(*value, 0.0, 100.0);
    bar->setValue(qRound(bounded * 10.0));
    bar->setFormat(percent(bounded));
    bar->setAccessibleDescription(percent(bounded));
}

QString DashboardWindow::statusText(const MetricStatus &status)
{
    return status.state == MetricState::Pending ? tr("Waiting…") : tr("N/A");
}

DashboardWindow::StorageRow &DashboardWindow::ensureStorageRow(const std::string &path)
{
    for (StorageRow &row : m_storageRows)
        if (row.path == path)
            return row;

    auto *container = new QWidget(this);
    auto *layout = new QGridLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(8);
    const bool home = !m_homePath.empty() && path == m_homePath;
    auto *name = label(home ? tr("Home") : path == "/" ? tr("Root") : q(path), container);
    auto *detail = label(home ? QStringLiteral("~") : QString(), container);
    QFont secondary = detail->font();
    secondary.setPointSizeF(secondary.pointSizeF() * 0.9);
    detail->setFont(secondary);
    auto *value = label(QStringLiteral("N/A"), container);
    value->setObjectName(home ? QStringLiteral("homeStorageValue") : QStringLiteral("rootStorageValue"));
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *bar = new QProgressBar(container);
    bar->setAccessibleName(home ? tr("Home storage usage") : tr("Root storage usage"));
    bar->setObjectName(home ? QStringLiteral("homeStorageProgress") : QStringLiteral("rootStorageProgress"));
    bar->setTextVisible(false);
    setProgress(bar, std::nullopt);
    layout->addWidget(name, 0, 0);
    layout->addWidget(value, 0, 1);
    layout->addWidget(detail, 1, 0, 1, 2);
    layout->addWidget(bar, 2, 0, 1, 2);
    m_storageLayout->addWidget(container);
    m_storageRows.append(StorageRow{path, value, detail, bar});
    return m_storageRows.last();
}

void DashboardWindow::updateSnapshot(const SystemMetricsSnapshot &s)
{
    if (s.cpu.status.ok() && s.cpu.usagePercent) {
        m_cpuValue->setText(percent(*s.cpu.usagePercent));
        m_cpuDetail->setText(s.cpu.logicalCpus > 0 ? tr("%1 logical CPUs").arg(s.cpu.logicalCpus) : QString());
        setProgress(m_cpuBar, s.cpu.usagePercent);
    } else {
        const QString text = statusText(s.cpu.status);
        m_cpuValue->setText(text);
        m_cpuDetail->clear();
        setProgress(m_cpuBar, std::nullopt, text);
    }

    if (s.memory.status.ok() && s.memory.info) {
        const MemoryInfo &memory = *s.memory.info;
        m_memoryValue->setText(percent(memory.usagePercent));
        m_memoryDetail->setText(tr("%1 / %2 used").arg(bytes(memory.usedBytes), bytes(memory.totalBytes)));
        if (memory.availableIsEstimate)
            m_memoryDetail->setText(m_memoryDetail->text() + tr(" (estimated)"));
        setProgress(m_memoryBar, memory.usagePercent);
        m_swapValue->setText(memory.swapPresent
                                 ? tr("%1 / %2").arg(bytes(memory.swapUsedBytes), bytes(memory.swapTotalBytes))
                                 : tr("Disabled"));
    } else {
        const QString text = statusText(s.memory.status);
        m_memoryValue->setText(text);
        m_memoryDetail->setText(text);
        m_swapValue->setText(text);
        setProgress(m_memoryBar, std::nullopt, text);
    }

    if (s.network.status.ok()) {
        m_networkDown->setText(QStringLiteral("↓ %1").arg(rate(s.network.downloadBytesPerSecond)));
        m_networkUp->setText(QStringLiteral("↑ %1").arg(rate(s.network.uploadBytesPerSecond)));
    } else {
        m_networkDown->setText(statusText(s.network.status));
        m_networkUp->setText(statusText(s.network.status));
    }

    for (const StorageMetrics &storage : s.storage) {
        StorageRow &row = ensureStorageRow(storage.path);
        if (storage.status.ok() && storage.info) {
            row.value->setText(percent(storage.info->usagePercent));
            row.detail->setToolTip(tr("%1 used, %2 available")
                                       .arg(bytes(storage.info->usedBytes), bytes(storage.info->availableBytes)));
            setProgress(row.bar, storage.info->usagePercent);
        } else {
            const QString text = statusText(storage.status);
            row.value->setText(text);
            row.detail->setToolTip(QString::fromStdString(storage.status.message));
            setProgress(row.bar, std::nullopt, text);
        }
    }

    if (s.diskIo.status.ok()) {
        m_diskRead->setText(QStringLiteral("↓ %1").arg(rate(s.diskIo.readBytesPerSecond)));
        m_diskWrite->setText(QStringLiteral("↑ %1").arg(rate(s.diskIo.writeBytesPerSecond)));
    } else {
        m_diskRead->setText(statusText(s.diskIo.status));
        m_diskWrite->setText(statusText(s.diskIo.status));
    }

    updateGpus(s.gpu);

    // FPS: only a fresh MangoHud reading is a value; every other condition
    // says why there is none, except OFF, which hides the row (and the
    // Gaming section, FPS being its only metric).
    const bool fpsShown = !fpsHidden(s.fps);
    m_fps.container->setVisible(fpsShown);
    m_gamingHeader->setVisible(fpsShown);
    if (s.fps.status.ok() && s.fps.fps) {
        QStringList detail{tr("MangoHud")};
        if (!s.fps.application.empty())
            detail << q(s.fps.application);
        if (s.fps.frametimeMs)
            detail << tr("%1 ms frame time").arg(*s.fps.frametimeMs, 0, 'f', 1);
        setText(m_fps, q(formatFps(*s.fps.fps)), detail.join(QStringLiteral(" · ")));
    } else {
        QString detail = q(s.fps.status.message);
        if (s.fps.condition == FpsCondition::Stale && s.fps.lastFps)
            detail += tr(" (last reading %1)").arg(q(formatFps(*s.fps.lastFps)));
        setText(m_fps, statusText(s.fps.status), detail, s.fps.condition == FpsCondition::Stale);
    }

    updateUpdates(m_packageUpdates, s.packageUpdates);
    updateUpdates(m_flatpakUpdates, s.flatpakUpdates);

    if (s.uptime.status.ok() && s.uptime.seconds)
        setText(m_uptime, q(formatDuration(*s.uptime.seconds)), QString());
    else
        setText(m_uptime, statusText(s.uptime.status), q(s.uptime.status.message));
    if (s.kernel.status.ok() && s.kernel.info)
        setText(m_kernel, q(s.kernel.info->release), q(s.kernel.info->sysname + " · " + s.kernel.info->machine), false,
                q(s.kernel.info->version));
    else
        setText(m_kernel, statusText(s.kernel.status), q(s.kernel.status.message));
    if (s.distribution.status.ok() && s.distribution.info) {
        const DistributionInfo &d = *s.distribution.info;
        QStringList detail;
        if (!d.versionId.empty() && d.prettyName.find(d.versionId) == std::string::npos)
            detail << q(d.versionId);
        if (!d.buildId.empty())
            detail << q(d.buildId);
        if (!d.id.empty())
            detail << tr("ID %1").arg(q(d.id));
        setText(m_distribution, q(d.prettyName.empty() ? d.id : d.prettyName), detail.join(QStringLiteral(" · ")), false,
                tr("From %1").arg(q(d.source)));
    } else {
        setText(m_distribution, statusText(s.distribution.status), q(s.distribution.status.message));
    }
}

void DashboardWindow::updateUpdates(TextRow &row, const UpdatesMetrics &updates)
{
    QStringList detail;
    if (!updates.manager.empty() && updates.manager != "flatpak")
        detail << q(updates.manager);
    if (updates.manager == "flatpak" && updates.count && *updates.count > 0)
        detail << (updates.apps == 1 ? tr("1 app") : tr("%1 apps").arg(updates.apps)) + QStringLiteral(", ")
                      + (updates.runtimes == 1 ? tr("1 runtime") : tr("%1 runtimes").arg(updates.runtimes));
    if (updates.ignored > 0)
        detail << tr("%1 ignored").arg(updates.ignored);
    const QLocale locale;
    if (updates.lastSuccess)
        detail << tr("checked %1").arg(locale.toString(QDateTime::fromSecsSinceEpoch(*updates.lastSuccess), QLocale::ShortFormat));
    if (updates.metadataStale && updates.metadataTime)
        detail << tr("package metadata from %1 is out of date")
                      .arg(locale.toString(QDateTime::fromSecsSinceEpoch(*updates.metadataTime).date(), QLocale::ShortFormat));
    if (updates.checking)
        detail << tr("checking…");
    if (!updates.note.empty())
        detail << q(updates.note);
    if (updates.status.ok() && updates.count) {
        setText(row, q(formatUpdateCount(*updates.count)), detail.join(QStringLiteral(" · ")), updates.metadataStale);
        return;
    }
    if (!updates.status.message.empty() && updates.status.state != MetricState::Pending)
        detail.prepend(q(updates.status.message));
    setText(row, updates.status.state == MetricState::Error ? tr("Error") : statusText(updates.status),
            detail.join(QStringLiteral(" · ")), updates.status.state == MetricState::Error, q(updates.status.message));
}

void DashboardWindow::updateGpus(const GpuMetrics &gpu)
{
    // Cards are rebuilt only when the set of devices changes.
    bool same = int(gpu.devices.size()) == m_gpuCards.size();
    for (int i = 0; same && i < m_gpuCards.size(); ++i)
        same = m_gpuCards.at(i).id == gpu.devices[std::size_t(i)].id;
    if (!same) {
        for (const GpuCard &card : std::as_const(m_gpuCards))
            delete card.card;
        m_gpuCards.clear();
        for (std::size_t i = 0; i < gpu.devices.size(); ++i) {
            GpuCard card;
            card.id = gpu.devices[i].id;
            card.card = new QWidget(this);
            card.card->setObjectName(QStringLiteral("gpu_%1").arg(i));
            auto *grid = new QGridLayout(card.card);
            grid->setContentsMargins(0, 0, 0, 0);
            grid->setVerticalSpacing(2);
            card.title = label(QString(), card.card, nullptr);
            card.title->setObjectName(QStringLiteral("gpuTitle_%1").arg(i));
            card.title->setWordWrap(true);
            card.detail = label(QString(), card.card, nullptr);
            card.detail->setObjectName(QStringLiteral("gpuDetail_%1").arg(i));
            QFont secondary = card.detail->font();
            secondary.setPointSizeF(secondary.pointSizeF() * 0.9);
            card.detail->setFont(secondary);
            card.utilizationName = label(tr("Utilization"), card.card, nullptr);
            card.utilization = label(QStringLiteral("N/A"), card.card, nullptr);
            card.utilization->setObjectName(QStringLiteral("gpuUtilization_%1").arg(i));
            card.utilizationBar = new QProgressBar(card.card);
            card.utilizationBar->setObjectName(QStringLiteral("gpuUtilizationProgress_%1").arg(i));
            card.utilizationBar->setTextVisible(false);
            card.memory = label(QStringLiteral("N/A"), card.card, nullptr);
            card.memory->setObjectName(QStringLiteral("gpuMemory_%1").arg(i));
            card.memoryBar = new QProgressBar(card.card);
            card.memoryBar->setObjectName(QStringLiteral("gpuMemoryProgress_%1").arg(i));
            card.memoryBar->setTextVisible(false);
            card.temperature = label(QStringLiteral("N/A"), card.card, nullptr);
            card.temperature->setObjectName(QStringLiteral("gpuTemperature_%1").arg(i));
            for (QLabel *value : {card.utilization, card.memory, card.temperature})
                value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            grid->addWidget(card.title, 0, 0, 1, 2);
            grid->addWidget(card.detail, 1, 0, 1, 2);
            grid->addWidget(card.utilizationName, 2, 0);
            grid->addWidget(card.utilization, 2, 1);
            grid->addWidget(card.utilizationBar, 3, 0, 1, 2);
            grid->addWidget(label(tr("Memory"), card.card, nullptr), 4, 0);
            grid->addWidget(card.memory, 4, 1);
            grid->addWidget(card.memoryBar, 5, 0, 1, 2);
            grid->addWidget(label(tr("Temperature"), card.card, nullptr), 6, 0);
            grid->addWidget(card.temperature, 6, 1);
            m_gpuLayout->addWidget(card.card);
            m_gpuCards.append(card);
        }
    }
    m_gpuStatus->setVisible(gpu.devices.empty());
    m_gpuStatus->setText(gpu.status.state == MetricState::Pending ? tr("Waiting…") : q(gpu.status.message));

    // A value only when the field has one; otherwise its reason, no bar.
    auto field = [](QLabel *value, QProgressBar *bar, const MetricStatus &status, const QString &text,
                    const std::optional<double> &percent) {
        value->setText(status.ok() ? text : statusText(status));
        value->setToolTip(status.ok() ? QString() : q(status.message));
        if (bar) {
            bar->setVisible(status.ok() && percent.has_value());
            if (status.ok() && percent)
                setProgress(bar, percent);
        }
    };
    for (int i = 0; i < m_gpuCards.size(); ++i) {
        const GpuDevice &device = gpu.devices[std::size_t(i)];
        GpuCard &card = m_gpuCards[i];
        card.title->setText(q(device.model));
        QStringList detail{q(device.id)};
        if (!device.driver.empty())
            detail << q(device.driver);
        if (device.integrated)
            detail << (*device.integrated ? tr("integrated") : tr("discrete"));
        if (device.frequencyMHz && *device.frequencyMHz > 0)
            detail << tr("%1 MHz").arg(qRound(*device.frequencyMHz));
        if (device.suspended)
            detail << tr("suspended");
        if (device.selected && gpu.devices.size() > 1)
            detail << tr("Metrics Bar GPU");
        card.detail->setText(detail.join(QStringLiteral(" · ")));
        const bool activity = device.utilizationMethod == "rc6-residency";
        card.utilizationName->setText(activity ? tr("Activity") : tr("Utilization"));
        card.utilizationName->setToolTip(activity ? tr("Time out of the RC6 power-saving state") : QString());
        field(card.utilization, card.utilizationBar, device.utilizationStatus,
              device.utilizationPercent ? percent(*device.utilizationPercent) : QString(), device.utilizationPercent);
        std::optional<double> memoryPercent;
        QString memoryText;
        if (device.memoryUsedBytes && device.memoryTotalBytes && *device.memoryTotalBytes > 0) {
            memoryPercent = 100.0 * double(*device.memoryUsedBytes) / double(*device.memoryTotalBytes);
            memoryText = tr("%1 / %2").arg(bytes(*device.memoryUsedBytes), bytes(*device.memoryTotalBytes));
        }
        field(card.memory, card.memoryBar, device.memoryStatus, memoryText, memoryPercent);
        field(card.temperature, nullptr, device.temperatureStatus,
              device.temperatureCelsius ? q(formatCelsius(*device.temperatureCelsius)) : QString(), std::nullopt);
    }
}

namespace {

bool hasValidCustomValue(const aqtruncat::custommetrics::Source &source)
{
    // A command skeleton (title/icon before the first result) has no items.
    return source.snapshot && !source.snapshot->metrics.isEmpty();
}

QString settledKey(const aqtruncat::custommetrics::Source &source)
{
    return source.id.isEmpty() ? source.path : source.id;
}

} // namespace

void DashboardWindow::updateCustomMetrics(const QList<aqtruncat::custommetrics::Source> &all)
{
    // v2F1.2: a disabled source is not presented (Settings still lists it).
    QList<aqtruncat::custommetrics::Source> sources;
    for (const aqtruncat::custommetrics::Source &source : all) {
        if (source.enabled)
            sources.append(source);
    }
    // Tracked on every update, also while hidden, so a deferred rebuild
    // presents the same state a visible Dashboard would.
    QHash<QString, SettledState> settled;
    for (const aqtruncat::custommetrics::Source &source : sources) {
        if (!hasValidCustomValue(source))
            continue;
        const QString key = settledKey(source);
        if (source.state != aqtruncat::custommetrics::State::Pending)
            settled.insert(key, {source.state, source.message});
        else if (const auto previous = m_customSettled.constFind(key); previous != m_customSettled.cend())
            settled.insert(key, *previous);
    }
    m_customSettled = std::move(settled);
    m_customSources = sources;
    m_customDirty = true;
    if (isVisible())
        rebuildCustomMetrics();
}

void DashboardWindow::rebuildCustomMetrics()
{
    m_customDirty = false;
    // Integrations first, as in Settings; each keeps the service's order.
    // Card indexes run across both sections.
    QList<const aqtruncat::custommetrics::Source *> ordered;
    for (const bool official : {true, false}) {
        for (const aqtruncat::custommetrics::Source &source : std::as_const(m_customSources)) {
            if (source.integration.isEmpty() != official)
                ordered.append(&source);
        }
    }
    bool sameList = ordered.size() == m_customCards.size();
    for (int i = 0; sameList && i < ordered.size(); ++i)
        sameList = m_customCards.at(i).source == settledKey(*ordered.at(i))
            && m_customCards.at(i).official == !ordered.at(i)->integration.isEmpty();
    if (sameList) {
        // Command sources republish on every run: only the cards that show
        // something new are replaced, in place; the window does not reflow
        // or flicker, and the scroll position stays.
        const bool updates = m_scroll->widget()->updatesEnabled();
        m_scroll->widget()->setUpdatesEnabled(false);
        for (int i = 0; i < ordered.size(); ++i) {
            CustomCard &card = m_customCards[i];
            const QString key = customCardKey(*ordered.at(i));
            if (key == card.key)
                continue;
            QWidget *replacement = customCard(*ordered.at(i), i);
            QVBoxLayout *layout = card.official ? m_integrationsLayout : m_customLayout;
            delete layout->replaceWidget(card.widget, replacement);
            delete card.widget;
            card.widget = replacement;
            card.key = key;
        }
        m_scroll->widget()->setUpdatesEnabled(updates);
        return;
    }

    m_customCards.clear();
    for (QVBoxLayout *layout : {m_integrationsLayout, m_customLayout}) {
        while (QLayoutItem *item = layout->takeAt(0)) {
            if (QWidget *widget = item->widget())
                delete widget;
            delete item;
        }
    }
    int index = 0;
    int integrations = 0;
    int custom = 0;
    for (const aqtruncat::custommetrics::Source *source : std::as_const(ordered)) {
        const bool official = !source->integration.isEmpty();
        QWidget *widget = customCard(*source, index++);
        (official ? m_integrationsLayout : m_customLayout)->addWidget(widget);
        m_customCards.append({settledKey(*source), official, customCardKey(*source), widget});
        ++(official ? integrations : custom);
    }
    m_integrationsHeader->setVisible(integrations > 0);
    m_integrationsContainer->setVisible(integrations > 0);
    m_customHeader->setVisible(custom > 0);
    m_customContainer->setVisible(custom > 0);
}

QWidget *DashboardWindow::customCard(const aqtruncat::custommetrics::Source &source, int index)
{
    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("customSource_%1").arg(index));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(0, 2, 0, 6);
    cardLayout->setSpacing(3);

    const QString fallbackTitle = QFileInfo(source.path).completeBaseName();
    const QString titleText = source.snapshot ? source.snapshot->title : fallbackTitle;
    QString heading = titleText;
    const QIcon icon = source.snapshot ? customSymbolIcon(source.snapshot->icon) : QIcon();
    const QString symbol = source.snapshot && icon.isNull()
        ? customSymbolText(source.snapshot->symbol) : QString();
    if (!symbol.isEmpty())
        heading = symbol + QStringLiteral("  ") + heading;
    auto *headingRow = new QWidget(card);
    auto *headingLayout = new QHBoxLayout(headingRow);
    headingLayout->setContentsMargins(0, 0, 0, 0);
    headingLayout->setSpacing(6);
    if (!icon.isNull()) {
        auto *iconLabel = new QLabel(headingRow);
        iconLabel->setObjectName(QStringLiteral("customIcon_%1").arg(index));
        iconLabel->setPixmap(icon.pixmap(16, 16));
        headingLayout->addWidget(iconLabel);
    }
    auto *title = label(heading, card);
    title->setObjectName(QStringLiteral("customTitle_%1").arg(index));
    QFont font = title->font();
    font.setBold(true);
    title->setFont(font);
    headingLayout->addWidget(title);
    headingLayout->addStretch();
    cardLayout->addWidget(headingRow);

    if (source.snapshot) {
        int itemIndex = 0;
        for (const aqtruncat::custommetrics::Item &metric : source.snapshot->metrics) {
            auto *row = new QWidget(card);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            if (metric.warning) {
                // Theme icon, with the style's own as fallback.
                const QIcon warningIcon = QIcon::fromTheme(QStringLiteral("dialog-warning"),
                                                           style()->standardIcon(QStyle::SP_MessageBoxWarning));
                auto *icon = new QLabel(row);
                icon->setObjectName(QStringLiteral("customWarning_%1_%2").arg(index).arg(itemIndex));
                icon->setPixmap(warningIcon.pixmap(16, 16));
                icon->setToolTip(metric.detail);
                icon->setAccessibleName(tr("Warning"));
                rowLayout->addWidget(icon);
            }
            rowLayout->addWidget(label(metric.title, row));
            rowLayout->addStretch();
            auto *value = label(metric.formattedValue, row);
            value->setObjectName(QStringLiteral("customValue_%1_%2").arg(index).arg(itemIndex));
            rowLayout->addWidget(value);
            cardLayout->addWidget(row);
            if (metric.normalizedValue) {
                auto *bar = new QProgressBar(card);
                bar->setTextVisible(false);
                bar->setObjectName(QStringLiteral("customProgress_%1_%2").arg(index).arg(itemIndex));
                bar->setAccessibleName(metric.title);
                setProgress(bar, std::clamp(*metric.normalizedValue, 0.0, 1.0) * 100.0);
                cardLayout->addWidget(bar);
            }
            if (!metric.detail.isEmpty()) {
                auto *detail = label(metric.detail, card);
                detail->setObjectName(QStringLiteral("customDetail_%1_%2").arg(index).arg(itemIndex));
                QFont secondary = detail->font();
                secondary.setPointSizeF(secondary.pointSizeF() * 0.9);
                detail->setFont(secondary);
                detail->setWordWrap(true);
                detail->setContentsMargins(0, 0, 0, 4); // apart from the next item
                cardLayout->addWidget(detail);
            }
            ++itemIndex;
        }
    }

    QString shownMessage;
    const aqtruncat::custommetrics::State shownState = presentedState(source, &shownMessage);

    QString footer;
    switch (shownState) {
    case aqtruncat::custommetrics::State::Available:
        footer = source.snapshot
            ? tr("Last updated: %1").arg(QLocale().toString(source.snapshot->lastUpdatedDate.toLocalTime(), QLocale::ShortFormat))
            : tr("Available");
        break;
    case aqtruncat::custommetrics::State::Pending:
        footer = tr("Waiting…");
        break;
    case aqtruncat::custommetrics::State::Unavailable:
        footer = tr("Unavailable");
        break;
    case aqtruncat::custommetrics::State::Error:
        footer = tr("Error");
        break;
    }
    auto *status = label(footer, card);
    status->setObjectName(QStringLiteral("customStatus_%1").arg(index));
    status->setToolTip(shownMessage);
    cardLayout->addWidget(status);
    return card;
}

aqtruncat::custommetrics::State DashboardWindow::presentedState(const aqtruncat::custommetrics::Source &source,
                                                                QString *message) const
{
    // A refresh with a previous valid value stays Pending internally but
    // keeps presenting the last settled state; only the first execution
    // (no valid value yet) shows "Waiting…".
    if (source.state == aqtruncat::custommetrics::State::Pending && hasValidCustomValue(source)) {
        const auto settled = m_customSettled.constFind(settledKey(source));
        *message = settled != m_customSettled.cend() ? settled->message : QString();
        return settled != m_customSettled.cend() ? settled->state : aqtruncat::custommetrics::State::Available;
    }
    *message = source.message;
    return source.state;
}

QString DashboardWindow::customCardKey(const aqtruncat::custommetrics::Source &source) const
{
    QString message;
    const aqtruncat::custommetrics::State state = presentedState(source, &message);
    QStringList parts{source.path, QString::number(int(state)), message};
    if (source.snapshot) {
        const aqtruncat::custommetrics::Snapshot &snapshot = *source.snapshot;
        parts << snapshot.title << snapshot.symbol.value_or(QString())
              << (snapshot.icon ? snapshot.icon->resolvedValue : QString())
              << snapshot.lastUpdatedDate.toString(Qt::ISODateWithMs);
        for (const aqtruncat::custommetrics::Item &item : snapshot.metrics)
            parts << item.title << item.formattedValue << item.detail
                  << (item.normalizedValue ? QString::number(*item.normalizedValue) : QStringLiteral("-"))
                  << QString::number(item.warning);
    }
    return parts.join(QChar(0x1f));
}

void DashboardWindow::toggleVisible()
{
    if (isVisible()) {
        hide();
    } else {
        show();
        raise();
        activateWindow();
    }
}

void DashboardWindow::closeEvent(QCloseEvent *event)
{
    hide();
    event->ignore();
}

void DashboardWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_customDirty)
        rebuildCustomMetrics();
    if (!m_shownOnce) {
        m_scroll->verticalScrollBar()->setValue(0);
        m_shownOnce = true;
    }
    emit visibilityChanged(true);
}

void DashboardWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    emit visibilityChanged(false);
}
