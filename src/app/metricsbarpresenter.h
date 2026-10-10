#pragma once

#include "metricsbarmodel.h"
#include "metricsbarrenderer.h"

#include <QAction>
#include <QCache>
#include <QIcon>
#include <QMenu>
#include <QObject>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QSystemTrayIcon;

// One tray entry of the Metrics Bar. The real one wraps QSystemTrayIcon
// (a StatusNotifierItem on Linux); tests substitute a recording fake.
class MetricsBarTrayItem : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual void setIcon(const QIcon &icon) = 0;
    virtual void setToolTip(const QString &toolTip) = 0;

signals:
    void activated();
};

using MetricsBarTrayItemFactory = std::function<std::unique_ptr<MetricsBarTrayItem>(QMenu *menu)>;

// SNI renderer of the Metrics Bar: separate tray items next to the runner's,
// never the runner's own item. Hosts tie-break equal titles by registration
// order (Plasma 6), so reordering re-creates the items after the first
// difference; everything else is updated in place. An icon is sent only when
// its rendered content changes; the tooltip only with an icon change or when
// something besides the precise value changes (see update()).
class MetricsBarPresenter : public QObject
{
    Q_OBJECT

public:
    explicit MetricsBarPresenter(MetricsBarTrayItemFactory factory = {}, QObject *parent = nullptr);
    ~MetricsBarPresenter() override;

    QStringList itemIds() const;
    int iconUpdates() const { return m_iconUpdates; }
    int toolTipUpdates() const { return m_toolTipUpdates; }
    int itemsCreated() const { return m_itemsCreated; }
    int iconsRendered() const { return m_iconsRendered; }

    static QString toolTipFor(const aqtruncat::metricsbar::Item &item);

public slots:
    void setItems(const QList<aqtruncat::metricsbar::Item> &items);
    void setDashboardVisible(bool visible);
    void clear();

signals:
    void dashboardToggleRequested();
    void settingsRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Entry {
        QString id;
        std::unique_ptr<QMenu> menu;
        std::unique_ptr<MetricsBarTrayItem> tray;
        aqtruncat::metricsbar::IconContent content;
        QString toolTip;
        std::optional<aqtruncat::metricsbar::Item> lastItem; // without detail
        bool rendered = false;
    };

    std::unique_ptr<Entry> createEntry(const QString &id);
    void update(Entry &entry, const aqtruncat::metricsbar::Item &item, bool forceIcon);
    void rerenderAll();
    QIcon iconFor(const aqtruncat::metricsbar::IconContent &content);

    MetricsBarTrayItemFactory m_factory;
    std::vector<std::unique_ptr<Entry>> m_entries;
    QList<aqtruncat::metricsbar::Item> m_items;
    QAction m_dashboardAction;
    QAction m_settingsAction;
    QAction m_quitAction;
    int m_iconUpdates = 0;
    int m_toolTipUpdates = 0;
    int m_itemsCreated = 0;
    int m_iconsRendered = 0;
    // Identical content reuses the identical QIcon. Every distinct QIcon sent
    // to a tray item leaves one pixmap in Qt's global, bounded QPixmapCache
    // (measured in M7); reuse keeps recurring values ("23%") from adding more.
    QCache<QString, QIcon> m_icons{128};
};
