#include "settingsactivation.h"

#include "settingswindow.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QLoggingCategory>

SettingsActivation::SettingsActivation(SettingsWindow *window, QObject *parent)
    : QObject(parent), m_window(window)
{
}

SettingsActivation::~SettingsActivation()
{
    if (!m_ownsService)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.unregisterObject(QLatin1String(aqtruncat::settings::ObjectPath));
    bus.unregisterService(QLatin1String(aqtruncat::settings::ServiceName));
}

bool SettingsActivation::acquire(QString *error)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        if (error)
            *error = QStringLiteral("No D-Bus session bus is available.");
        return false;
    }
    const auto activateExisting = [&bus] {
        QDBusMessage message = QDBusMessage::createMethodCall(
            QLatin1String(aqtruncat::settings::ServiceName), QLatin1String(aqtruncat::settings::ObjectPath),
            QLatin1String(aqtruncat::settings::InterfaceName), QStringLiteral("Show"));
        message.setAutoStartService(false);
        bus.asyncCall(message);
    };
    if (bus.interface()->isServiceRegistered(QLatin1String(aqtruncat::settings::ServiceName))) {
        activateExisting();
        return false;
    }
    // Export on this connection's unique name first. Registering the well-
    // known name only after the object exists prevents a simultaneous
    // launcher from observing a service owner whose Show method is not ready.
    if (!bus.registerObject(QLatin1String(aqtruncat::settings::ObjectPath), this,
                            QDBusConnection::ExportAllSlots)) {
        if (error)
            *error = QStringLiteral("Cannot register the Settings activation endpoint: %1")
                         .arg(bus.lastError().message());
        return false;
    }
    if (!bus.registerService(QLatin1String(aqtruncat::settings::ServiceName))) {
        bus.unregisterObject(QLatin1String(aqtruncat::settings::ObjectPath));
        if (!bus.interface()->isServiceRegistered(QLatin1String(aqtruncat::settings::ServiceName))) {
            if (error)
                *error = bus.lastError().message();
            return false;
        }
        activateExisting();
        return false;
    }
    m_ownsService = true;
    return true;
}

void SettingsActivation::Show()
{
    if (m_window)
        m_window->showAndRaise();
}

bool aqtruncat::settings::requestShow(QString *error)
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        if (error)
            *error = QStringLiteral("No D-Bus session bus is available.");
        return false;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(
        QLatin1String(ServiceName), QLatin1String(ObjectPath), QLatin1String(InterfaceName), QStringLiteral("Show"));
    message.setAutoStartService(true);
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message));
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
                     [watcher] {
                         if (watcher->isError())
                             qWarning().noquote() << QStringLiteral("cannot activate AQTRunCat Settings: %1")
                                                        .arg(watcher->error().message());
                         watcher->deleteLater();
                     });
    return true;
}
