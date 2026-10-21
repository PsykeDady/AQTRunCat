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

    BackendClient {
        id: backend
        // Metric deltas are parsed only while the metrics are on screen.
        wantMetrics: root.expanded || !root.compactRepresentationItem || !root.compactRepresentationItem.visible
    }

    Plasmoid.icon: "aqtruncat"
    toolTipMainText: "AQTRunCat"
    toolTipSubText: backend.connected ? (root.runner ? root.runner.displayName : "Connected")
                  : backend.status === "connecting" ? "Connecting to the backend…"
                  : backend.status === "incompatible" ? "Incompatible backend"
                  : "Backend not running"

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
