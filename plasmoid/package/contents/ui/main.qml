/*
 * AQTRunCat Plasmoid (F4.2): a native Plasma 6 frontend of the AQTRunCat
 * backend (aqtruncatd). It owns no metric engine: no sampling, no Custom
 * Metrics, no Claude/Codex adapters, no second settings store. Everything
 * comes from the backend's D-Bus API (BackendClient.qml); the widget only
 * presents it and animates the runner locally at the fps the backend
 * computed. Widget-only presentation options live in Plasma's own
 * configuration (contents/config/main.xml).
 */
import QtQuick
import QtQuick.Layouts
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami

import "../code/logic.js" as Logic

PlasmoidItem {
    id: root

    readonly property var runner: Logic.runnerToShow(backend.runnerCatalog)
    // F6: the pointer (or the keyboard focus) is on the panel runner: the
    // tooltip shows the summary, so the metrics are followed meanwhile.
    property bool hovered: false

    BackendClient {
        id: backend
        // Metric deltas are parsed only while the metrics are on screen,
        // in the popup or in the tooltip.
        wantMetrics: root.expanded || root.hovered || !root.compactRepresentationItem
                     || !root.compactRepresentationItem.visible
    }

    Plasmoid.icon: "aqtruncat"
    // Plasma's own applet tooltip (placed by the panel for any edge).
    toolTipMainText: "AQTRunCat"
    toolTipSubText: Logic.hoverText(backend.status, root.runner ? root.runner.displayName : "",
                                    backend.metrics, backend.metricsCurrent)
    toolTipTextFormat: Text.PlainText

    // Large on the desktop: the metrics in place; small, or in a panel: the
    // runner, and the metrics in the popup.
    switchWidth: Kirigami.Units.gridUnit * 12
    switchHeight: Kirigami.Units.gridUnit * 12

    compactRepresentation: CompactRepresentation {
        plasmoidItem: root
        client: backend
        runner: root.runner
        animate: Plasmoid.configuration.animate
    }

    fullRepresentation: FullRepresentation {
        client: backend
        runner: root.runner
        showBuiltin: Plasmoid.configuration.showBuiltin
        showIntegrations: Plasmoid.configuration.showIntegrations
        showCustom: Plasmoid.configuration.showCustom
        showDetails: Plasmoid.configuration.showDetails
    }
}
