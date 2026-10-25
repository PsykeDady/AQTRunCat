/*
 * AQTRunCat Plasmoid (v2F1.7): fills a Custom Metric's icon; a click asks
 * the backend for a fresh sample (BackendClient.refreshCustomMetric). Only
 * the icon, and only the left button: the rest of the row, the panel's
 * drag and the popup keep their own clicks. Disabled: not there at all.
 */
import QtQuick
import org.kde.plasma.components as PlasmaComponents

MouseArea {
    id: area

    property bool group: false
    signal triggered()

    anchors.fill: parent
    visible: enabled
    acceptedButtons: Qt.LeftButton
    hoverEnabled: true
    cursorShape: Qt.PointingHandCursor
    onClicked: triggered()

    Accessible.role: Accessible.Button
    Accessible.name: toolTip.text
    Accessible.onPressAction: triggered()

    PlasmaComponents.ToolTip {
        id: toolTip
        text: area.group ? "Refresh the group now" : "Refresh now"
        visible: area.containsMouse
    }
}
