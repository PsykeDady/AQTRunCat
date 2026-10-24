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
    // v2F1.5: how deep a source is in groups (for the indentation below).
    function groupDepth(metric) {
        let depth = 0;
        let parent = metric.data ? metric.data.groupId : "";
        while (parent && depth < 8) {
            const group = customSources.find(m => m.data && m.data.sourceId === parent);
            if (!group)
                break;
            ++depth;
            parent = group.data.groupId;
        }
        return depth;
    }
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

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: builtInForm.visible
        }

        // F5 (schema 2): absent with an older backend.
        Kirigami.FormLayout {
            id: builtInForm
            Layout.fillWidth: true
            enabled: client.connected
            visible: client.settings.gpuDevice !== undefined

            readonly property var gpuChoices: Logic.gpuChoices(client.metrics, client.settings.gpuDevice)
            readonly property var intervalChoices: Logic.updatesIntervalChoices()

            Item {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: "Built-in Metrics"
            }
            QQC2.ComboBox {
                Kirigami.FormData.label: "Metrics Bar GPU:"
                model: builtInForm.gpuChoices
                textRole: "text"
                valueRole: "value"
                currentIndex: Logic.indexOfValue(builtInForm.gpuChoices, client.settings.gpuDevice)
                onActivated: index => client.setSetting("gpuDevice", builtInForm.gpuChoices[index].value)
            }
            QQC2.ComboBox {
                Kirigami.FormData.label: "FPS source:"
                model: [{ text: "MangoHud log", value: "mangohud" }, { text: "Off", value: "off" }]
                textRole: "text"
                valueRole: "value"
                currentIndex: client.settings.fpsSource === "off" ? 1 : 0
                onActivated: index => client.setSetting("fpsSource", index === 1 ? "off" : "mangohud")
            }
            QQC2.ComboBox {
                Kirigami.FormData.label: "Check for updates:"
                model: builtInForm.intervalChoices
                textRole: "text"
                valueRole: "value"
                currentIndex: Logic.indexOfValue(builtInForm.intervalChoices, client.settings.updatesIntervalMinutes)
                onActivated: index => client.setSetting("updatesIntervalMinutes", builtInForm.intervalChoices[index].value)
            }
            QQC2.CheckBox {
                text: "Download fresh package metadata (uses the network)"
                enabled: client.settings.updatesIntervalMinutes > 0
                checked: client.settings.updatesRemoteRefresh === true
                onToggled: client.setSetting("updatesRemoteRefresh", checked)
            }
            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.7
                text: "Update checks only read: by default the metadata the system already downloaded. FPS comes from the log MangoHud writes while it logs a game."
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
            ColumnLayout {
                id: customSource
                required property var modelData
                Layout.fillWidth: true
                Layout.leftMargin: page.groupDepth(modelData) * Kirigami.Units.gridUnit
                spacing: 0
                RowLayout {
                    readonly property var modelData: customSource.modelData
                    // v2F1.2 / v2F1.5: the file's own "enabled" (a checked child of a
                    // disabled group stays inactive, shown faded).
                    readonly property bool own: modelData.data && typeof modelData.data.configuredEnabled === "boolean"
                                                ? modelData.data.configuredEnabled : modelData.enabled !== false
                    Layout.fillWidth: true
                    QQC2.CheckBox {
                        visible: typeof modelData.enabled === "boolean"
                        checked: parent.own
                        enabled: client.connected
                        QQC2.ToolTip.text: checked ? "On: the metric runs and is shown" : "Off: the metric does not run and is not shown"
                        QQC2.ToolTip.visible: hovered
                        onToggled: {
                            client.setCustomMetricEnabled(modelData.data.explicitPath || modelData.data.path, checked);
                            // The backend's state decides (a refused write comes back unchanged).
                            checked = Qt.binding(() => parent.own);
                        }
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        opacity: modelData.enabled === false ? 0.6 : 1
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
                // v2F1.6: its warnings under it (the metric still works), as in
                // the tray's Settings.
                Repeater {
                    model: Logic.metricWarnings(customSource.modelData)
                    RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        Kirigami.Icon {
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            Layout.alignment: Qt.AlignTop
                            source: "dialog-warning"
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: "Warning: " + parent.modelData
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            font: Kirigami.Theme.smallFont
                        }
                    }
                }
            }
        }
        QQC2.Button {
            enabled: client.connected
            icon.name: "list-add"
            text: "Add Custom Metrics file…"
            onClicked: fileDialog.open()
        }
        // v2F1.6: rescans the files; the result shows in the message above.
        QQC2.Button {
            enabled: client.connected && !client.reloadingCustomMetrics
            icon.name: "view-refresh"
            text: client.reloadingCustomMetrics ? "Reloading Custom Metrics…" : "Reload Custom Metrics"
            QQC2.ToolTip.text: "Read the Custom Metrics files again and apply the metrics and groups that were added, changed or removed"
            QQC2.ToolTip.visible: hovered
            onClicked: client.reloadCustomMetrics()
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
