#pragma once

#include <QObject>
#include <QString>

class SettingsWindow;

class SettingsActivation final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.psykedady.AQTRunCat.Settings")

public:
    explicit SettingsActivation(SettingsWindow *window, QObject *parent = nullptr);
    ~SettingsActivation() override;

    // Returns true when this process owns the Settings endpoint. If another
    // instance owns it, that instance receives Show and this process exits.
    bool acquire(QString *error = nullptr);

public slots:
    void Show();

private:
    SettingsWindow *m_window = nullptr;
    bool m_ownsService = false;
};

namespace aqtruncat::settings {

inline constexpr auto ServiceName = "io.github.psykedady.AQTRunCat.Settings";
inline constexpr auto ObjectPath = "/io/github/psykedady/AQTRunCat/Settings";
inline constexpr auto InterfaceName = "io.github.psykedady.AQTRunCat.Settings";

// Asks the session bus to activate AQTRunCat Settings if needed and show its
// window. Returns false only when the session bus cannot accept the request.
bool requestShow(QString *error = nullptr);

} // namespace aqtruncat::settings
