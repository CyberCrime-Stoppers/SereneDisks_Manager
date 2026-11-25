/*
 * SereneDisk Manager — UsageBar.qml
 * Horizontal capacity bar showing a usage fraction (0.0–1.0).
 * Plain Rectangle composition — calm, flat, no default-control quirks.
 */
import QtQuick
import QtQuick.Controls.Basic
import SDM

Rectangle {
    id: root

    // 0.0–1.0; values ≤ 0 render as an empty (unknown) track.
    property real fraction: 0
    property string label: ""

    implicitWidth: 200
    implicitHeight: label.length > 0 ? 30 : 8

    // Track
    color: Theme.bgHover
    radius: 4

    // Fill
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.max(0, Math.min(1, root.fraction)) * root.width
        radius: 4
        color: root.fraction > 0.9 ? Theme.warning : Theme.positive
        visible: root.fraction > 0
        Behavior on width { NumberAnimation { duration: 120 } }
    }

    Label {
        visible: root.label.length > 0
        anchors.right: parent.right
        anchors.bottom: parent.top
        anchors.bottomMargin: 2
        text: root.label
        font.pixelSize: Theme.fontTiny
        color: Theme.textSecondary
    }
}
