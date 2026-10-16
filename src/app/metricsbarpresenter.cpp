#include "metricsbarpresenter.h"

#include "applicationquit.h"
#include "runnerpresentation.h"

#include <QApplication>
#include <QEvent>
#include <QLocale>
#include <QPalette>
#include <QPixmap>
#include <QStyleHints>
#include <QSystemTrayIcon>

#include <algorithm>

using namespace aqtruncat::metricsbar;

namespace {

class SystemTrayItem : public MetricsBarTrayItem
{
public:
    explicit SystemTrayItem(QMenu *menu)
    {
        m_tray.setContextMenu(menu);
        connect(&m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            // Same rule as the runner item: a normal left click is Trigger.
            if (reason == QSystemTrayIcon::Trigger)
                emit activated();
        });
    }

    void setIcon(const QIcon &icon) override
    {
        m_tray.setIcon(icon);
        // Shown only once it has an icon, so the host never displays an
        // empty placeholder.
        if (!m_tray.isVisible())
            m_tray.show();
    }

    void setToolTip(const QString &toolTip) override { m_tray.setToolTip(toolTip); }

private:
    QSystemTrayIcon m_tray;
};

QString statusName(Status status)
{
    switch (status) {
    case Status::Pending: return MetricsBarPresenter::tr("Waiting");
    case Status::Available: return MetricsBarPresenter::tr("Available");
    case Status::Unavailable: return MetricsBarPresenter::tr("Unavailable");
    case Status::Error: return MetricsBarPresenter::tr("Error");
    }
    return {};
}

} // namespace

MetricsBarPresenter::MetricsBarPresenter(MetricsBarTrayItemFactory factory, QObject *parent)
    : QObject(parent)
    , m_factory(std::move(factory))
    , m_dashboardAction(tr("Show Dashboard"))
    , m_settingsAction(tr("Settings…"))
    , m_quitAction(tr("Quit AQTRunCat"))
{
    if (!m_factory)
        m_factory = [](QMenu *menu) { return std::make_unique<SystemTrayItem>(menu); };
    connect(&m_dashboardAction, &QAction::triggered, this, &MetricsBarPresenter::dashboardToggleRequested);
    connect(&m_settingsAction, &QAction::triggered, this, &MetricsBarPresenter::settingsRequested);
    connect(&m_quitAction, &QAction::triggered, this, [] { requestApplicationQuit(); });
    // Text colour follows the panel palette, also on runtime scheme changes.
    qApp->installEventFilter(this);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] { rerenderAll(); });
}

MetricsBarPresenter::~MetricsBarPresenter()
{
    clear();
}

QStringList MetricsBarPresenter::itemIds() const
{
    QStringList ids;
    for (const auto &entry : m_entries)
        ids.append(entry->id);
    return ids;
}

QString MetricsBarPresenter::toolTipFor(const Item &item)
{
    QStringList lines;
    const QString value = item.detail.isEmpty() ? statusName(item.status) : item.detail;
    lines.append(QStringLiteral("%1: %2").arg(item.label, value));
    if (item.status != Status::Available) {
        lines.append(item.statusMessage.isEmpty() ? statusName(item.status)
                                                  : QStringLiteral("%1: %2").arg(statusName(item.status), item.statusMessage));
    }
    if (item.lastUpdated.isValid())
        lines.append(tr("Last updated: %1").arg(QLocale().toString(item.lastUpdated.toLocalTime(), QLocale::ShortFormat)));
    lines.append(tr("AQTRunCat Metrics Bar"));
    return lines.join(u'\n');
}

void MetricsBarPresenter::setItems(const QList<Item> &items)
{
    m_items = items;

    // Drop entries that are no longer wanted.
    std::erase_if(m_entries, [&](const std::unique_ptr<Entry> &entry) {
        return std::none_of(items.cbegin(), items.cend(), [&](const Item &item) { return item.id == entry->id; });
    });

    // Keep the longest prefix already in the right order. Hosts append new
    // items after existing ones, so everything after the first difference is
    // re-created in order; the runner's item is never touched.
    std::size_t keep = 0;
    while (keep < m_entries.size() && keep < std::size_t(items.size()) && m_entries[keep]->id == items[int(keep)].id)
        ++keep;
    m_entries.resize(keep);

    for (int i = 0; i < items.size(); ++i) {
        if (std::size_t(i) >= m_entries.size())
            m_entries.push_back(createEntry(items[i].id));
        update(*m_entries[std::size_t(i)], items[i], false);
    }
}

std::unique_ptr<MetricsBarPresenter::Entry> MetricsBarPresenter::createEntry(const QString &id)
{
    auto entry = std::make_unique<Entry>();
    entry->id = id;
    entry->menu = std::make_unique<QMenu>();
    entry->menu->addAction(&m_dashboardAction);
    entry->menu->addAction(&m_settingsAction);
    entry->menu->addSeparator();
    entry->menu->addAction(&m_quitAction);
    entry->tray = m_factory(entry->menu.get());
    connect(entry->tray.get(), &MetricsBarTrayItem::activated, this, &MetricsBarPresenter::dashboardToggleRequested);
    ++m_itemsCreated;
    return entry;
}

void MetricsBarPresenter::update(Entry &entry, const Item &item, bool forceIcon)
{
    const IconContent content = iconContentFor(item);
    const bool iconChanged = !entry.rendered || forceIcon || content != entry.content;

    // On SNI hosts a tooltip change costs about as much as an icon change
    // (the host re-reads the item). So the precise value in the tooltip is
    // refreshed together with the icon, and otherwise only when something
    // other than that value changes (status, message, label, update time):
    // 23.1 % → 23.4 % keeps showing "23%" and sends nothing at all.
    Item withoutDetail = item;
    withoutDetail.detail.clear();
    if (iconChanged || !entry.lastItem || withoutDetail != *entry.lastItem) {
        const QString toolTip = toolTipFor(item);
        if (toolTip != entry.toolTip) {
            // Set first, so a newly shown item never appears without it.
            entry.toolTip = toolTip;
            entry.tray->setToolTip(toolTip);
            ++m_toolTipUpdates;
        }
        entry.lastItem = withoutDetail;
    }
    if (!iconChanged)
        return;
    entry.content = content;
    entry.rendered = true;
    entry.tray->setIcon(iconFor(content));
    ++m_iconUpdates;
}

QIcon MetricsBarPresenter::iconFor(const IconContent &content)
{
    const QColor colour = trayForegroundColour();
    const QFont font = QApplication::font();
    const qreal dpr = qApp->devicePixelRatio();
    const QString iconKey = content.icon
        ? QStringLiteral("%1:%2").arg(int(content.icon->type)).arg(content.icon->resolvedValue)
        : QString();
    const QString key = QStringLiteral("%1\x1f%2\x1f%3\x1f%4\x1f%5\x1f%6")
                            .arg(content.caption, content.text, iconKey,
                                 colour.name(QColor::HexArgb), font.key())
                            .arg(dpr);
    if (const QIcon *cached = m_icons.object(key))
        return *cached;
    const RenderedIcon icon = renderIcon(content, font, colour, dpr);
    ++m_iconsRendered;
    auto *result = new QIcon(QPixmap::fromImage(icon.image));
    const QIcon copy = *result;
    m_icons.insert(key, result);
    return copy;
}

void MetricsBarPresenter::rerenderAll()
{
    m_icons.clear();
    for (std::size_t i = 0; i < m_entries.size() && i < std::size_t(m_items.size()); ++i)
        update(*m_entries[i], m_items[int(i)], true);
}

void MetricsBarPresenter::setDashboardVisible(bool visible)
{
    m_dashboardAction.setText(visible ? tr("Hide Dashboard") : tr("Show Dashboard"));
}

void MetricsBarPresenter::clear()
{
    m_entries.clear();
    m_items.clear();
}

bool MetricsBarPresenter::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && (event->type() == QEvent::ApplicationPaletteChange
                            || event->type() == QEvent::ApplicationFontChange))
        rerenderAll();
    return QObject::eventFilter(watched, event);
}
