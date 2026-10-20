#include "dashboardwindow.h"

#include "customsymbol.h"
#include "metrics/metricsformat.h"

#include <QCloseEvent>
#include <QFont>
#include <QGridLayout>
#include <QHideEvent>
#include <QShowEvent>
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

void DashboardWindow::updateCustomMetrics(const QList<aqtruncat::custommetrics::Source> &sources)
{
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
    for (QVBoxLayout *layout : {m_integrationsLayout, m_customLayout}) {
        while (QLayoutItem *item = layout->takeAt(0)) {
            if (QWidget *widget = item->widget())
                delete widget;
            delete item;
        }
    }
    // Integrations first, as in Settings; each keeps the service's order.
    // Card indexes run across both sections.
    int index = 0;
    int integrations = 0;
    int custom = 0;
    for (const bool official : {true, false}) {
        for (const aqtruncat::custommetrics::Source &source : std::as_const(m_customSources)) {
            if (source.integration.isEmpty() == official)
                continue;
            (official ? m_integrationsLayout : m_customLayout)->addWidget(customCard(source, index++));
            ++(official ? integrations : custom);
        }
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

    // A refresh with a previous valid value stays Pending internally but
    // keeps presenting the last settled state; only the first execution
    // (no valid value yet) shows "Waiting…".
    aqtruncat::custommetrics::State shownState = source.state;
    QString shownMessage = source.message;
    if (source.state == aqtruncat::custommetrics::State::Pending && hasValidCustomValue(source)) {
        const auto settled = m_customSettled.constFind(settledKey(source));
        shownState = settled != m_customSettled.cend() ? settled->state
                                                       : aqtruncat::custommetrics::State::Available;
        shownMessage = settled != m_customSettled.cend() ? settled->message : QString();
    }

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
