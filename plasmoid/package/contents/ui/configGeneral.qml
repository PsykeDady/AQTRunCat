/*
 * AQTRunCat Plasmoid (F4.2): the shared AQTRunCat settings, through the
 * backend's API. They are the same settings as the tray application's
 * Settings window and "aqtruncatctl settings": one AQTRunCat.ini owned by
 * the backend, so a change here applies at once (no Apply button) and
 * shows up in every frontend; a change made elsewhere shows up here.
 * Nothing on this page is stored in the widget's own configuration.
 */
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami

import "../code/logic.js" as Logic

KCM.SimpleKCM {
    id: page

    // Plasma hands every page all widget options; this page uses none.
    property bool cfg_animate
    property bool cfg_animateDefault
    property bool cfg_showBuiltin
    property bool cfg_showBuiltinDefault
    property bool cfg_showIntegrations
    property bool cfg_showIntegrationsDefault
    property bool cfg_showCustom
    property bool cfg_showCustomDefault
    property bool cfg_showDetails
    property bool cfg_showDetailsDefault

    BackendClient {
        id: client
        onReplied: (member, ok, message) => {
            if (!ok) {
                feedback.type = Kirigami.MessageType.Error;
                feedback.text = message;
                feedback.visible = true;
            } else if (message.length > 0) {
                feedback.type = Kirigami.MessageType.Positive;
                feedback.text = message;
                feedback.visible = true;
            }
        }
    }

    readonly property var catalog: client.runnerCatalog
    readonly property var runners: catalog && Array.isArray(catalog.runners) ? catalog.runners : []
    readonly property var customSources: client.metrics.filter(m => m.category === "custom")
    readonly property var networkInterfaces: {
        const download = client.metrics.find(m => m.id === "network.download");
        const list = download && download.data && Array.isArray(download.data.interfaces) ? download.data.interfaces : [];
        return list.map(i => i.name);
    }

    Component.onCompleted: client.reloadRunners()

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !client.connected
            type: client.status === "connecting" ? Kirigami.MessageType.Information : Kirigami.MessageType.Warning
            text: client.status === "connecting" ? "Connecting to the AQTRunCat backend…"
                : client.status === "incompatible" ? client.statusMessage
                : "The AQTRunCat backend (aqtruncatd) is not running: settings cannot be changed now. " + client.statusMessage
        }

        Kirigami.InlineMessage {
            id: feedback
            Layout.fillWidth: true
            showCloseButton: true
            visible: false
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            enabled: client.connected

            QQC2.Label {
                Kirigami.FormData.label: "Backend:"
                text: client.connected
                      ? "aqtruncatd " + (client.info.version || "?") + " (API " + client.info.api + "), shared with the tray application and aqtruncatctl"
                      : client.status
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Item { Kirigami.FormData.isSection: true }

            QQC2.ComboBox {
                id: runnerBox
                Kirigami.FormData.label: "Runner:"
                visible: client.hasRunnerCatalog
                model: page.runners
                textRole: "displayName"
                valueRole: "id"
                currentIndex: page.runners.findIndex(r => r.id === (page.catalog ? page.catalog.selected : ""))
                delegate: QQC2.ItemDelegate {
                    required property var modelData
                    required property int index
                    width: runnerBox.width
                    enabled: modelData.valid
                    text: modelData.displayName + (modelData.source === "user" ? " (user)" : "")
                          + (modelData.valid ? "" : " — invalid: " + modelData.error)
                    highlighted: runnerBox.highlightedIndex === index
                }
                onActivated: index => client.setSetting("runnerId", page.runners[index].id)
            }
            QQC2.Label {
                Kirigami.FormData.label: "Runner:"
                visible: !client.hasRunnerCatalog
                text: (client.runner.runnerId || "?") + " (this backend cannot list runners; update AQTRunCat)"
            }

            QQC2.ComboBox {
                Kirigami.FormData.label: "Speed:"
                model: [{ text: "Normal", value: "normal" }, { text: "Slow down", value: "slow-down" }]
                textRole: "text"
                valueRole: "value"
                currentIndex: client.settings.speedMode === "slow-down" ? 1 : 0
                onActivated: index => client.setSetting("speedMode", index === 1 ? "slow-down" : "normal")
            }

            Item { Kirigami.FormData.isSection: true }

            QQC2.RadioButton {
                Kirigami.FormData.label: "Network:"
                text: "Automatic (physical interfaces)"
                checked: client.settings.networkMode !== "manual"
                onToggled: if (checked) client.setSetting("networkMode", "automatic")
            }
            QQC2.RadioButton {
                id: manualNetwork
                text: "Manual selection"
                checked: client.settings.networkMode === "manual"
                onToggled: if (checked) client.setSetting("networkMode", "manual")
            }
            Repeater {
                model: manualNetwork.checked ? page.networkInterfaces : []
                QQC2.CheckBox {
                    required property string modelData
                    Layout.leftMargin: Kirigami.Units.gridUnit
                    text: modelData
                    checked: (client.settings.networkInterfaces || []).indexOf(modelData) >= 0
                    onToggled: {
                        const current = (client.settings.networkInterfaces || []).filter(n => n !== modelData);
                        if (checked)
                            current.push(modelData);
                        client.setSetting("networkInterfaces", current);
                    }
                }
            }
        }

        Kirigami.Separator { Layout.fillWidth: true }

        Kirigami.Heading {
            level: 3
            text: "Integrations"
        }
        Repeater {
            model: client.integrations
            Kirigami.FormLayout {
                id: integration
                required property var modelData
                Layout.fillWidth: true
                enabled: client.connected && modelData.adapterInstalled

                QQC2.Switch {
                    Kirigami.FormData.label: integration.modelData.title + ":"
                    text: integration.modelData.adapterInstalled ? "Enabled" : "Not installed"
                    checked: integration.modelData.enabled
                    onToggled: client.setIntegrationEnabled(integration.modelData.id, checked)
                }
                QQC2.Switch {
                    text: "Live updates"
                    enabled: integration.modelData.enabled
                    checked: integration.modelData.liveUpdates === "on"
                    onToggled: client.setIntegrationLiveUpdates(integration.modelData.id, checked)
                }
                QQC2.Label {
                    visible: text.length > 0
                    text: integration.modelData.liveError || (integration.modelData.enabled && !integration.modelData.providerFound
                          ? integration.modelData.title + " was not found on this computer." : "")
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }

        Kirigami.Separator { Layout.fillWidth: true }

        Kirigami.Heading {
            level: 3
            text: "Custom Metrics"
        }
        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: "Files found in " + (client.info.customMetricsDirectory || "the discovery directory")
                  + " are used automatically; other files can be registered here."
        }
        Repeater {
            model: page.customSources
            RowLayout {
                required property var modelData
                Layout.fillWidth: true
                QQC2.Label {
                    Layout.fillWidth: true
                    text: modelData.title + "  ·  " + (modelData.data ? modelData.data.path : "")
                    elide: Text.ElideMiddle
                    textFormat: Text.PlainText
                }
                QQC2.Button {
                    visible: modelData.data && modelData.data.explicit === true
                    icon.name: "list-remove"
                    text: "Remove"
                    onClicked: client.unregisterCustomMetric(modelData.data.explicitPath || modelData.data.path)
                }
            }
        }
        QQC2.Button {
            enabled: client.connected
            icon.name: "list-add"
            text: "Add Custom Metrics file…"
            onClicked: fileDialog.open()
        }
        FileDialog {
            id: fileDialog
            title: "Custom Metrics file"
            nameFilters: ["JSON files (*.json)"]
            onAccepted: client.registerCustomMetric(decodeURIComponent(selectedFile.toString().replace(/^file:\/\//, "")))
        }

        Kirigami.Separator { Layout.fillWidth: true }

        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            opacity: 0.7
            text: "Start at login, the Metrics Bar and the Dashboard refresh belong to the AQTRunCat tray application: "
                  + "change them in its Settings window or with \"aqtruncatctl settings set\"."
        }
    }
}
