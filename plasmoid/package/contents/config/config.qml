import QtQuick
import org.kde.plasma.configuration

ConfigModel {
    ConfigCategory {
        name: "AQTRunCat"
        icon: "aqtruncat"
        source: "configGeneral.qml"
    }
    ConfigCategory {
        name: "Appearance"
        icon: "preferences-desktop-color"
        source: "configAppearance.qml"
    }
}
