// Offscreen preview of the AQTRunCat Plasmoid's own components (F4.2), for
// tools/plasmoid-preview.sh: the real BackendClient, RunnerView and
// FullRepresentation against a real backend, outside plasmashell. Saves one
// PNG with the popup and the runner at panel sizes, then quits.
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

import "../plasmoid/package/contents/ui" as UI
import "../plasmoid/package/contents/code/logic.js" as Logic

Window {
    id: window
    width: 760
    height: 1000
    visible: true
    color: Kirigami.Theme.backgroundColor

    readonly property string output: Qt.application.arguments[Qt.application.arguments.length - 1]
    readonly property var runner: Logic.runnerToShow(backend.runnerCatalog)

    UI.BackendClient { id: backend }

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.gridUnit

        Rectangle {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 22
            Layout.fillHeight: true
            color: Kirigami.Theme.backgroundColor
            border.color: Kirigami.Theme.disabledTextColor
            UI.FullRepresentation {
                anchors.fill: parent
                anchors.margins: 1
                client: backend
                runner: window.runner
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            spacing: Kirigami.Units.gridUnit
            Repeater {
                // Panel thicknesses: horizontal 22/34/48 px, vertical 34/48 px.
                model: [[0, 22], [0, 34], [0, 48], [34, 0], [48, 0]]
                Rectangle {
                    required property var modelData
                    readonly property real aspect: window.runner ? window.runner.aspect : 1
                    Layout.preferredWidth: modelData[0] > 0 ? modelData[0] : Math.round(modelData[1] * aspect)
                    Layout.preferredHeight: modelData[1] > 0 ? modelData[1] : Math.round(modelData[0] / aspect)
                    color: Kirigami.Theme.alternateBackgroundColor
                    UI.RunnerView {
                        anchors.fill: parent
                        runner: window.runner
                        fps: Number(backend.runner.fps) || 0
                    }
                }
            }
            Kirigami.Heading {
                level: 4
                text: backend.status + (window.runner ? " · " + window.runner.displayName + " · " + backend.runner.fps + " fps" : "")
            }
        }
    }

    Timer {
        interval: 6000
        running: true
        onTriggered: content.grabToImage(result => {
            result.saveToFile(window.output);
            Qt.quit();
        })
    }
}
