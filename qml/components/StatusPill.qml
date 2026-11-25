/*
 * SereneDisk Manager — StatusPill.qml
 * A quiet, reusable status chip. Color is always passed in by the caller
 * (theme-quiet: positive / warning / danger / accent) so the pill itself
 * never decides severity — the page does.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import SDM

Rectangle {
    id: root

    property string text: ""
    property color tint: Theme.textMuted
    property string tooltip: ""

    implicitWidth: pillRow.implicitWidth + Theme.spaceM * 2
    implicitHeight: 22
    radius: height / 2
    color: Qt.rgba(tint.r, tint.g, tint.b, 0.15)

    RowLayout {
        id: pillRow
        anchors.centerIn: parent
        spacing: Theme.spaceXS

        Rectangle {
            visible: root.text.length > 0
            Layout.preferredWidth: 6
            Layout.preferredHeight: 6
            radius: 3
            color: root.tint
        }

        Label {
            text: root.text
            font.pixelSize: Theme.fontTiny
            font.bold: true
            color: root.tint
        }
    }

    HoverHandler {
        id: hover
        enabled: root.tooltip.length > 0
    }
    ToolTip.visible: hover.enabled && hover.hovered
    ToolTip.text: root.tooltip
    ToolTip.delay: 400
}
