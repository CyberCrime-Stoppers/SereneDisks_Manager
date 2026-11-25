/*
 * SereneDisk Manager — DeviceCard.qml
 * Card for one physical drive: identity, capacity, calm health pill.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import SDM

// StatusPill resolves as a same-directory sibling type.

Rectangle {
    id: root

    required property var disk   // one entry from backend.diskList
    property bool checked: false
    signal clicked()

    implicitHeight: layout.implicitHeight + Theme.spaceL * 2
    radius: Theme.radiusM
    color: root.checked ? Theme.bgElevated
          : mouse.containsMouse ? Theme.bgSurface
                                : Theme.bgSurface
    border.width: root.checked ? 1 : 0
    border.color: Theme.accent

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.spaceL
        spacing: Theme.spaceS

        RowLayout {
            spacing: Theme.spaceM

            Rectangle {
                Layout.preferredWidth: 44
                Layout.preferredHeight: 44
                radius: Theme.radiusM
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.15)
                Label {
                    anchors.centerIn: parent
                    text: disk.rotational ? "⛁" : "▤"
                    font.pixelSize: 20
                    color: Theme.accent
                }
            }

            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                Label {
                    text: (disk.vendor + " " + disk.model).trim() ||
                          disk.path.split("/").pop()
                    font.pixelSize: Theme.fontBig
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: Theme.humanBytes(disk.sizeBytes) +
                          (disk.connection ? "  ·  " + disk.connection : "")
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                }
            }

            Rectangle {
                // Quiet capacity indicator — no alarming fills.
                Layout.preferredWidth: 90
                Layout.preferredHeight: 22
                radius: height / 2
                color: Qt.rgba(Theme.positive.r, Theme.positive.g,
                              Theme.positive.b, 0.15)
                Label {
                    anchors.centerIn: parent
                    text: qsTr("%1 partitions").arg(disk.partitions)
                    font.pixelSize: Theme.fontTiny
                    color: Theme.positive
                }
            }

            // Built-in temperature sensor chip — only when the drive
            // reports a reading. Sensor source in the tooltip.
            StatusPill {
                visible: disk.smart &&
                         disk.smart.temperatureC !== undefined &&
                         disk.smart.temperatureC !== null
                text: Theme.tempLabel(disk.smart.temperatureC)
                tint: Theme.tempColor(disk.smart.temperatureC)
                tooltip: disk.smart.sensor === "nvme"
                         ? qsTr("Reported by the NVMe controller")
                         : qsTr("Reported by the drive (S.M.A.R.T.)")
            }
        }
    }
}
