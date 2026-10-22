/*
 * AQTRunCat Plasmoid (F4.2): one metric of the popup. Everything shown is
 * the backend's text (formattedValue, detail, message); the widget formats
 * nothing itself and never turns a missing value into 0: no bar without a
 * normalized value, "—" without a value, the backend's message for
 * pending/unavailable/error. Multi-value metrics (Claude Code and Codex
 * quota windows, multi-value snapshots) list their items, each with its
 * own bar, reset line and warning ("Out of date", "cannot be calculated").
 */
import QtQuick
import QtQuick.Layouts
import org.kde.plasma.components as PlasmaComponents
import org.kde.kirigami as Kirigami

import "../code/logic.js" as Logic

ColumnLayout {
    id: metric

    required property string metricId
    required property string title
    required property string state
    required property string value
    required property string detail
    required property bool hasFraction
    required property real fraction
    required property bool warning
    required property string icon
    required property string itemsJson
    property bool showDetails: true

    readonly property var items: {
        try {
            return JSON.parse(itemsJson);
        } catch (e) {
            return [];
        }
    }
    readonly property bool ok: state === "ok"
    // F6: with the detail lines hidden, hovering the row shows them.
    readonly property string toolTipText: Logic.rowToolTip(ok && !warning ? detail : "", ok ? items : [], showDetails)

    spacing: Kirigami.Units.smallSpacing

    HoverHandler {
        id: hover
    }

    PlasmaComponents.ToolTip {
        text: metric.toolTipText
        visible: hover.hovered && text.length > 0
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: title + ": " + (ok ? value : detail)

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            visible: metric.icon.length > 0 || !metric.ok || metric.warning
            source: metric.state === "error" ? "dialog-error"
                  : metric.state === "unavailable" || metric.warning ? "dialog-warning"
                  : metric.state === "pending" ? "content-loading-symbolic"
                  : metric.icon
        }
        PlasmaComponents.Label {
            Layout.fillWidth: true
            text: metric.title
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }
        PlasmaComponents.Label {
            text: metric.value
            textFormat: Text.PlainText
            font.bold: metric.ok && !metric.warning
            opacity: metric.ok && !metric.warning ? 1 : 0.6
        }
    }

    PlasmaComponents.ProgressBar {
        Layout.fillWidth: true
        visible: metric.hasFraction
        from: 0
        to: 1
        value: metric.fraction
        Accessible.name: metric.title
    }

    PlasmaComponents.Label {
        Layout.fillWidth: true
        visible: text.length > 0 && (metric.showDetails || !metric.ok || metric.warning)
        text: metric.detail
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        font: Kirigami.Theme.smallFont
        opacity: 0.7
    }

    Repeater {
        model: metric.ok ? metric.items : []

        ColumnLayout {
            id: item
            required property var modelData
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.gridUnit
            spacing: 0

            Accessible.role: Accessible.StaticText
            Accessible.name: modelData.title + ": " + modelData.value + (modelData.warning ? " (" + modelData.detail + ")" : "")

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Kirigami.Icon {
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    visible: item.modelData.warning
                    source: "dialog-warning"
                }
                PlasmaComponents.Label {
                    Layout.fillWidth: true
                    text: item.modelData.title
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                PlasmaComponents.Label {
                    text: item.modelData.value
                    textFormat: Text.PlainText
                    opacity: item.modelData.warning ? 0.7 : 1
                }
            }
            PlasmaComponents.ProgressBar {
                Layout.fillWidth: true
                visible: item.modelData.fraction !== null
                from: 0
                to: 1
                value: item.modelData.fraction === null ? 0 : item.modelData.fraction
            }
            PlasmaComponents.Label {
                Layout.fillWidth: true
                visible: text.length > 0 && (metric.showDetails || item.modelData.warning)
                text: item.modelData.detail
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }
        }
    }
}
