/*
 * SereneDisk Manager — DevicesPage.qml
 * Left: scrollable list of drive cards. Right: selected drive detail.
 * Read-only for M0 — look, don't touch.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "components"

Item {
    id: root
    property string pageName: "devices"

    property var selected: null

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceL
        spacing: Theme.spaceL

        // ---- Drive list --------------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 340
            spacing: Theme.spaceS

            RowLayout {
                spacing: Theme.spaceS
                Layout.fillWidth: true
                Label {
                    text: qsTr("Devices")
                    font.pixelSize: Theme.fontTitle
                    font.bold: true
                    color: Theme.textPrimary
                }
                Item { Layout.fillWidth: true }
                Button {
                    flat: true
                    text: qsTr("⟳")
                    onClicked: devicesBackend.refresh()
                   ToolTip.visible: hovered
                    ToolTip.text: qsTr("Rescan devices")
                }
            }

            ListView {
                id: driveList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: Theme.spaceS
                model: devicesBackend.diskList
                ScrollBar.vertical: ScrollBar {}

                delegate: DeviceCard {
                    required property var modelData
                    width: ListView.view.width
                    disk: modelData
                    checked: root.selected?.path === modelData.path
                    onClicked: root.selected = modelData
                }
            }
        }

        // ---- Detail pane ---------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusL
            color: Theme.bgSurface

            ColumnLayout {
                visible: root.selected
                anchors.fill: parent
                anchors.margins: Theme.spaceXL
                spacing: Theme.spaceM

                Label {
                    text: root.selected
                          ? ((root.selected.vendor + " " +
                              root.selected.model).trim() ||
                             root.selected.path)
                          : ""
                    font.pixelSize: Theme.fontTitle
                    font.bold: true
                    color: Theme.textPrimary
                }

                Label {
                    text: root.selected
                          ? qsTr("Serial %1 · %2 · %3")
                                .arg(root.selected.serial || "—")
                                .arg(Theme.humanBytes(root.selected.sizeBytes))
                                .arg(Theme.kindLabel(root.selected.kind,
                                                     root.selected.rotational))
                          : ""
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                    wrapMode: Text.WrapAnywhere
                }

                UsageBar {
                    Layout.fillWidth: true
                    fraction: 0.42
                    label: qsTr("Partition space allocated")  // M1: real usage
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusM
                    color: Theme.bgWindow

                    Label {
                        anchors.centerIn: parent
                        visible: !(root.selected &&
                                   root.selected.partitions > 0)
                        text: qsTr("No partition table detected")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontBody
                    }

                    ColumnLayout {
                        visible: root.selected &&
                                 root.selected.partitions > 0
                        anchors.fill: parent
                        anchors.margins: Theme.spaceL
                        spacing: Theme.spaceS

                        Label {
                            text: qsTr("Partitions (%1)")
                                      .arg(root.selected.partitions)
                            font.pixelSize: Theme.fontBig
                            color: Theme.textPrimary
                        }

                        // Per-partition rows arrive with M1 partition model;
                        // count summary keeps the layout honest for M0.
                        Label {
                            text: qsTr("Detailed partition view arrives in M1.")
                            font.pixelSize: Theme.fontSmall
                            color: Theme.textMuted
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: !root.selected
                text: qsTr("Select a device to inspect it")
                color: Theme.textMuted
                font.pixelSize: Theme.fontBig
            }
        }
    }
}
