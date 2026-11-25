/*
 * SereneDisk Manager — PartitionMap.qml
 * Proportional map of a disk's on-disk layout: one horizontal strip where
 * each partition is drawn at its true offset and width, so inter-partition
 * gaps and head/tail slack render as unallocated space — the picture IS
 * the geometry, not a simplified list.
 *
 * Inputs:
 *   partitions  — list of maps with offsetBytes/sizeBytes/number/name/
 *                 fsLabel/fsType/encrypted/mounted (backend partitionDetails)
 *   totalBytes  — disk capacity; defines the 100% span of the strip
 *
 * Color semantics (calm, never alarming):
 *   mounted    → accent violet
 *   encrypted  → warning amber
 *   plain      → quiet accent tint
 *   unallocated → track surface with dashed mood via border
 */
import QtQuick
import QtQuick.Controls.Basic
import SDM

Item {
    id: root

    property var partitions: []
    property real totalBytes: 0

    readonly property int stripHeight: 20
    readonly property int minSegWidth: 3   // even a 2 MiB ESP stays visible

    implicitWidth: 200
    implicitHeight: stripHeight + Theme.spaceS + legendRow.height

    // ---- Segment appearance helpers --------------------------------------
    function segColor(p) {
        if (p.encrypted) return Theme.warning
        if (p.mounted)   return Theme.accent
        return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.38)
    }

    function segLabel(p) {
        const name = p.name || p.fsLabel || p.fsType || p.devicePath
        const state = p.encrypted ? qsTr(" · encrypted")
                    : p.mounted   ? qsTr(" · mounted")
                                  : ""
        return qsTr("%1  %2  %3%4")
            .arg(qsTr("Part %1").arg(p.number))
            .arg(name)
            .arg(Theme.humanBytes(p.sizeBytes))
            .arg(state)
    }

    // ---- The strip ---------------------------------------------------------
    Rectangle {
        id: strip
        width: parent.width
        height: stripHeight
        radius: height / 2
        color: Theme.bgHover
        border.width: 1
        border.color: Theme.border
        clip: true

        // Free-span overlay: draws nothing itself — the track background
        // showing through between segments IS the unallocated space.
        Repeater {
            model: root.partitions

            Rectangle {
                id: seg
                required property var modelData
                required property int index

                x: root.totalBytes > 0
                   ? (modelData.offsetBytes / root.totalBytes) * strip.width
                   : 0
                width: root.totalBytes > 0
                       ? Math.max(minSegWidth,
                                  (modelData.sizeBytes / root.totalBytes) *
                                  strip.width)
                       : 0
                anchors.top: parent.top
                anchors.topMargin: 2
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 2
                radius: 3
                color: root.segColor(modelData)
                opacity: segHover.containsMouse ? 1.0 : 0.92

                Behavior on opacity { NumberAnimation { duration: 100 } }

                MouseArea {
                    id: segHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                }

                ToolTip.visible: segHover.containsMouse
                ToolTip.delay: 200
                ToolTip.text: root.segLabel(modelData)
            }
        }

        // Entirely raw disk: mark the track so it doesn't read as a widget
        Label {
            anchors.centerIn: parent
            visible: root.partitions.length === 0
            text: qsTr("raw")
            font.pixelSize: Theme.fontTiny
            color: Theme.textMuted
        }
    }

    // ---- Legend --------------------------------------------------------------
    Row {
        id: legendRow
        anchors.top: strip.bottom
        anchors.topMargin: Theme.spaceS
        anchors.left: parent.left
        spacing: Theme.spaceM

        LegendChip { color: Theme.accent;   text: qsTr("Mounted") }
        LegendChip { color: Qt.rgba(Theme.accent.r, Theme.accent.g,
                                    Theme.accent.b, 0.38)
                     text: qsTr("Unmounted") }
        LegendChip { color: Theme.warning;  text: qsTr("Encrypted") }
        LegendChip { color: Theme.bgHover;  bordered: true
                     text: qsTr("Unallocated") }
    }

    component LegendChip: Row {
        id: chip
        property color color: Theme.accent
        property string text: ""
        property bool bordered: false

        Rectangle {
            width: 10
            height: 10
            radius: 3
            anchors.verticalCenter: parent.verticalCenter
            color: chip.color
            border.width: chip.bordered ? 1 : 0
            border.color: Theme.border
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: chip.text
            font.pixelSize: Theme.fontTiny
            color: Theme.textSecondary
            leftPadding: 4
        }
    }
}
