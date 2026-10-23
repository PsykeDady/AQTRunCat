/*
 * AQTRunCat Plasmoid (F4.2): the popup (Plasma's expanded representation),
 * or the widget itself on the desktop when it is large enough. Metrics in
 * the backend's three categories — Built-in Metrics, Integrations, Custom Metrics —
 * by their "category" field only; an empty or hidden category has no
 * section. Not a copy of the Qt Dashboard: one scrollable Plasma list.
 */
import QtQuick
import QtQuick.Layouts
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami

import "../code/logic.js" as Logic

PlasmaExtras.Representation {
    id: full

    required property var client
    property bool showBuiltin: true
    property bool showIntegrations: true
    property bool showCustom: true
    property bool showDetails: true
    // Set by main.qml: the runner shown in the panel (null: none).
    property var runner: null

    Layout.minimumWidth: Kirigami.Units.gridUnit * 16
    Layout.minimumHeight: Kirigami.Units.gridUnit * 12
    Layout.preferredWidth: Kirigami.Units.gridUnit * 22
    Layout.preferredHeight: Kirigami.Units.gridUnit * 26
    collapseMarginsHint: true

    readonly property var shown: ({ builtin: showBuiltin, integration: showIntegrations, custom: showCustom })

    // Last settled state of each Custom Metric, so that a command running
    // again does not collapse its row (Logic.settleRefreshes).
    property var settled: ({})

    function sync() {
        const presented = Logic.settleRefreshes(client.metrics, settled);
        settled = presented.settled;
        Logic.syncModel(metricModel, Logic.metricRows(presented.metrics, shown));
    }
    onShownChanged: sync()
    Connections {
        target: full.client
        function onMetricsChanged() { full.sync(); }
    }
    Component.onCompleted: sync()

    ListModel { id: metricModel }

    header: PlasmaExtras.PlasmoidHeading {
        RowLayout {
            anchors.fill: parent
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.smallSpacing
                level: 3
                text: "AQTRunCat"
                elide: Text.ElideRight
            }
            PlasmaComponents.Label {
                text: full.client.connected && full.runner
                      ? full.runner.displayName + " · " + (Number(full.client.runner.fps) || 0) + " fps"
                      : ""
                visible: text.length > 0
                opacity: 0.7
                textFormat: Text.PlainText
            }
            PlasmaComponents.ToolButton {
                // Plasma's own "Configure AQTRunCat…" action of this widget.
                readonly property var configureAction: typeof Plasmoid.internalAction === "function"
                                              ? Plasmoid.internalAction("configure") : null
                visible: configureAction !== null
                icon.name: "configure"
                display: PlasmaComponents.AbstractButton.IconOnly
                text: configureAction ? configureAction.text : ""
                onClicked: configureAction.trigger()
                PlasmaComponents.ToolTip.text: text
                PlasmaComponents.ToolTip.visible: hovered && text.length > 0
            }
        }
    }

    PlasmaComponents.ScrollView {
        id: scroll
        anchors.fill: parent
        visible: full.client.connected && metricModel.count > 0
        // No horizontal scrolling; the content width follows the room left
        // by the vertical scroll bar (as Plasma's own applets do), so that
        // wrapping text and the scroll bar do not feed back on each other.
        PlasmaComponents.ScrollBar.horizontal.policy: PlasmaComponents.ScrollBar.AlwaysOff
        contentWidth: availableWidth - list.leftMargin - list.rightMargin

        contentItem: ListView {
            id: list
            model: metricModel
            clip: true
            focus: true
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: Kirigami.Units.largeSpacing
            topMargin: Kirigami.Units.smallSpacing
            bottomMargin: Kirigami.Units.smallSpacing
            leftMargin: Kirigami.Units.largeSpacing
            rightMargin: Kirigami.Units.largeSpacing

            section.property: "category"
            section.delegate: PlasmaExtras.ListSectionHeader {
                required property string section
                width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
                text: section === "builtin" ? "Built-in Metrics" : section === "integration" ? "Integrations" : "Custom Metrics"
            }

            delegate: MetricDelegate {
                width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
                showDetails: full.showDetails
            }
        }
    }

    // Connected, nothing to show (every category hidden or empty).
    PlasmaExtras.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 2
        visible: full.client.connected && metricModel.count === 0
        iconName: "view-hidden"
        text: "No metrics to show"
        explanation: "Every category is hidden or empty. Choose the categories in the widget's settings."
    }

    PlasmaExtras.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 2
        visible: !full.client.connected
        iconName: full.client.status === "connecting" ? "network-connect"
                : full.client.status === "incompatible" ? "dialog-error" : "network-disconnect"
        text: full.client.status === "connecting" ? "Connecting to AQTRunCat…"
            : full.client.status === "incompatible" ? "Incompatible AQTRunCat backend"
            : "AQTRunCat backend not running"
        explanation: full.client.status === "connecting" ? ""
                   : full.client.statusMessage + (full.client.status === "unavailable"
                       ? "\nThe widget keeps retrying; it is started on demand by D-Bus activation (aqtruncatd)." : "")
        helpfulAction: full.client.status === "connecting" ? null : retryAction
    }

    Kirigami.Action {
        id: retryAction
        icon.name: "view-refresh"
        text: "Retry"
        onTriggered: {
            full.client.resetRetry();
            full.client.refresh();
        }
    }

    footer: PlasmaExtras.PlasmoidHeading {
        position: PlasmaComponents.ToolBar.Footer
        visible: full.client.connected && full.client.runnerCatalog === null && !full.client.hasRunnerCatalog
        PlasmaComponents.Label {
            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing
            wrapMode: Text.Wrap
            font: Kirigami.Theme.smallFont
            text: "This AQTRunCat backend (" + (full.client.info.version || "?") + ") cannot list runners: the panel shows the AQTRunCat icon. Update AQTRunCat to see the runner."
        }
    }
}
