// v2F1.7: a row of the Plasmoid's popup (contents/ui/MetricDelegate.qml),
// with qmltestrunner and Plasma's QML modules (no D-Bus). A Custom Metric's
// icon, a group's and a child's own icon ask for a refresh; a built-in
// metric's icon, the rest of the row and the right button do not.
import QtQuick
import QtTest

import "../../plasmoid/package/contents/ui"

Item {
    width: 500
    height: 600

    MetricDelegate {
        id: command
        width: 400
        metricId: "custom:/m/a.json"
        title: "A"
        state: "ok"
        value: "1"
        detail: ""
        hasFraction: false
        fraction: 0
        warning: false
        icon: "dialog-information"
        itemsJson: "[]"
        group: false
        warnings: ""
        refreshable: true
    }

    MetricDelegate {
        id: builtin
        y: 100
        width: 400
        metricId: "cpu"
        title: "CPU"
        state: "ok"
        value: "22 %"
        detail: ""
        hasFraction: false
        fraction: 0
        warning: false
        icon: "cpu"
        itemsJson: "[]"
        group: false
        warnings: ""
        refreshable: false
    }

    MetricDelegate {
        id: group
        y: 200
        width: 400
        metricId: "custom:/m/g/metric.json"
        title: "G"
        state: "ok"
        value: ""
        detail: ""
        hasFraction: false
        fraction: 0
        warning: false
        icon: "dialog-information"
        itemsJson: JSON.stringify([
            { metricId: "custom:/m/g/x/metric.json", refreshable: true, icon: "", title: "X", value: "1",
              fraction: null, state: "ok", detail: "", warnings: [] },
            { metricId: "custom:/m/g/y/metric.json", refreshable: true, icon: "dialog-warning", title: "Y", value: "2",
              fraction: null, state: "ok", detail: "", warnings: [] }])
        group: true
        warnings: ""
        refreshable: true
    }

    SignalSpy { id: commandSpy; target: command; signalName: "refreshRequested" }
    SignalSpy { id: builtinSpy; target: builtin; signalName: "refreshRequested" }
    SignalSpy { id: groupSpy; target: group; signalName: "refreshRequested" }

    TestCase {
        name: "PlasmoidDelegate"
        when: windowShown

        function test_commandIcon() {
            const area = findChild(command, "refreshArea");
            verify(area);
            verify(area.visible);
            compare(area.cursorShape, Qt.PointingHandCursor);
            mouseClick(area);
            compare(commandSpy.count, 1);
            compare(commandSpy.signalArguments[0][0], "custom:/m/a.json");
            mouseClick(area, area.width / 2, area.height / 2, Qt.RightButton);
            compare(commandSpy.count, 1);
            mouseClick(command, command.width - 4, 4); // the value, not the icon
            compare(commandSpy.count, 1);
        }

        function test_builtinIconDoesNothing() {
            const area = findChild(builtin, "refreshArea");
            verify(area);
            verify(!area.visible);
            mouseClick(builtin, area.parent.x + area.parent.width / 2, area.parent.y + area.parent.height / 2);
            compare(builtinSpy.count, 0);
        }

        function test_groupAndChildIcons() {
            waitForRendering(group);
            groupSpy.clear();
            mouseClick(findChild(group, "refreshArea"));
            compare(groupSpy.count, 1);
            compare(groupSpy.signalArguments[0][0], "custom:/m/g/metric.json");
            // Only Y has an icon of its own: its cell's icon refreshes Y.
            const areas = [];
            const collect = item => {
                for (let i = 0; i < item.children.length; ++i) {
                    const c = item.children[i];
                    if (c.objectName === "childRefreshArea")
                        areas.push(c);
                    collect(c);
                }
            };
            collect(group);
            compare(areas.length, 2);
            verify(!areas[0].visible); // X shares the group's icon: none in its cell
            const child = areas[1];
            verify(child);
            mouseClick(child);
            compare(groupSpy.count, 2);
            compare(groupSpy.signalArguments[1][0], "custom:/m/g/y/metric.json");
        }
    }
}
