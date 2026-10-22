/*
 * AQTRunCat Plasmoid (F4.2): what the panel shows. Only the runner: in a
 * horizontal panel it takes the panel's thickness as height and its own
 * aspect ratio for the width, in a vertical panel the other way round, on
 * the desktop it fits the widget. A click opens the popup (Plasma's own
 * expanded representation); hovering shows Plasma's applet tooltip with
 * the CPU/memory summary (F6, main.qml).
 */
import QtQuick
import QtQuick.Layouts
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami

MouseArea {
    id: compact

    required property var plasmoidItem
    required property var client
    required property var runner
    required property bool animate

    readonly property bool vertical: Plasmoid.formFactor === PlasmaCore.Types.Vertical
    readonly property bool horizontal: Plasmoid.formFactor === PlasmaCore.Types.Horizontal
    readonly property real aspect: runnerView.hasRunner ? runnerView.aspect : 1

    Layout.minimumWidth: horizontal ? Math.round(height * aspect) : Kirigami.Units.iconSizes.small
    Layout.maximumWidth: horizontal ? Layout.minimumWidth : Infinity
    Layout.minimumHeight: vertical ? Math.round(width / aspect) : Kirigami.Units.iconSizes.small
    Layout.maximumHeight: vertical ? Layout.minimumHeight : Infinity

    hoverEnabled: true
    acceptedButtons: Qt.LeftButton
    // F6: the tooltip summary needs current metrics while it can be seen.
    Binding {
        target: compact.plasmoidItem
        property: "hovered"
        value: compact.containsMouse || compact.activeFocus
    }
    property bool wasExpanded: false
    onPressed: wasExpanded = compact.plasmoidItem.expanded
    onClicked: compact.plasmoidItem.expanded = !wasExpanded

    Accessible.role: Accessible.Button
    Accessible.name: compact.plasmoidItem.toolTipMainText
    Accessible.description: compact.plasmoidItem.toolTipSubText
    Accessible.onPressAction: compact.plasmoidItem.expanded = !compact.plasmoidItem.expanded
    activeFocusOnTab: true
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Enter || event.key === Qt.Key_Return) {
            compact.plasmoidItem.expanded = !compact.plasmoidItem.expanded;
            event.accepted = true;
        }
    }

    RunnerView {
        id: runnerView
        anchors.fill: parent
        runner: compact.runner
        fps: compact.client.connected ? Number(compact.client.runner.fps) || 0 : 0
        animating: compact.animate && compact.client.connected
        // Disconnected: the runner stands still and fades a little.
        opacity: compact.client.connected ? 1 : 0.5
    }
}
