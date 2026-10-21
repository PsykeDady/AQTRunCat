/*
 * AQTRunCat Plasmoid (F4.2): the animated runner, played locally.
 *
 * The backend sends no frames: GetRunners says which files to load and in
 * which order, RunnerChanged the fps it computed from the CPU load. Each
 * frame is painted once into its own Canvas (again only when the size or
 * the colour changes); animating only switches which one is visible, so a
 * tick costs no decoding, no allocation and no D-Bus traffic.
 *
 * Monochrome runners keep their alpha and take the theme's text colour
 * (source-in, as the Qt tray does); colour runners are drawn as they are.
 */
import QtQuick
import QtQuick.Window
import org.kde.kirigami as Kirigami

import "../code/logic.js" as Logic

Item {
    id: view

    // Logic.runnerToShow() result, or null.
    property var runner: null
    property int fps: 0
    property bool animating: true
    property color color: Kirigami.Theme.textColor

    readonly property real aspect: runner ? runner.aspect : 1
    readonly property bool hasRunner: runner !== null && runner.frames.length > 0
    property int step: 0
    readonly property int frameIndex: hasRunner ? runner.sequence[step % runner.sequence.length] : -1

    // The frames fill the largest rectangle of the runner's aspect ratio.
    readonly property real paintWidth: Math.max(1, Math.min(width, height * aspect))
    readonly property real paintHeight: Math.max(1, paintWidth / aspect)

    onRunnerChanged: step = 0

    Repeater {
        model: view.hasRunner ? view.runner.frames : []

        Canvas {
            id: frame
            required property string modelData
            required property int index

            x: Math.round((view.width - width) / 2)
            y: Math.round((view.height - height) / 2)
            width: Math.round(view.paintWidth)
            height: Math.round(view.paintHeight)
            visible: index === view.frameIndex
            renderStrategy: Canvas.Immediate

            property color tint: view.color
            property bool mask: view.runner ? view.runner.monochrome : true
            // Painted in device pixels: again on a screen of another scale.
            property real screenScale: Screen.devicePixelRatio
            onScreenScaleChanged: requestPaint()
            onTintChanged: requestPaint()
            onMaskChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            Component.onCompleted: loadImage(modelData)
            onImageLoaded: requestPaint()
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                if (!isImageLoaded(modelData))
                    return;
                ctx.drawImage(modelData, 0, 0, width, height);
                if (mask) {
                    ctx.globalCompositeOperation = "source-in";
                    ctx.fillStyle = tint;
                    ctx.fillRect(0, 0, width, height);
                }
            }
        }
    }

    // No runner (old backend without a runner catalogue, invalid runner):
    // the application icon, never an empty panel slot.
    Kirigami.Icon {
        anchors.fill: parent
        visible: !view.hasRunner
        source: "aqtruncat"
        fallback: "utilities-system-monitor"
    }

    Timer {
        interval: Math.max(16, Logic.frameInterval(view.fps))
        repeat: true
        running: view.animating && view.visible && view.hasRunner && view.runner.sequence.length > 1
                 && Logic.frameInterval(view.fps) > 0
        onTriggered: view.step = (view.step + 1) % view.runner.sequence.length
    }
}
