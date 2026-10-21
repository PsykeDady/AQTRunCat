/*
 * AQTRunCat Plasmoid (F4.2): options of this widget instance only (Plasma's
 * configuration, contents/config/main.xml), applied with Plasma's usual
 * Apply/OK. Each widget on a panel or desktop has its own.
 */
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami

KCM.SimpleKCM {
    property alias cfg_animate: animate.checked
    property alias cfg_showBuiltin: showBuiltin.checked
    property alias cfg_showIntegrations: showIntegrations.checked
    property alias cfg_showCustom: showCustom.checked
    property alias cfg_showDetails: showDetails.checked

    Kirigami.FormLayout {
        QQC2.CheckBox {
            id: animate
            Kirigami.FormData.label: "Panel:"
            text: "Animate the runner"
        }
        QQC2.Label {
            text: "Its speed follows the CPU load, as computed by the AQTRunCat backend."
            opacity: 0.7
            font: Kirigami.Theme.smallFont
        }

        Item { Kirigami.FormData.isSection: true }

        QQC2.CheckBox {
            id: showBuiltin
            Kirigami.FormData.label: "Popup:"
            text: "System metrics"
        }
        QQC2.CheckBox {
            id: showIntegrations
            text: "Integrations (Claude Code, Codex)"
        }
        QQC2.CheckBox {
            id: showCustom
            text: "Custom Metrics"
        }
        QQC2.CheckBox {
            id: showDetails
            text: "Detail lines (used / total, reset times)"
        }
    }
}
