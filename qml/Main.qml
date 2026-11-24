/*
 * SereneDisk Manager — Main.qml
 * Application shell: sidebar navigation + content stack.
 * Reads from the backend singleton exposed by main.cpp.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "components"

ApplicationWindow {
    id: root
    width: 1080
    height: 700
    minimumWidth: 860
    minimumHeight: 560
    visible: true
    title: qsTr("SereneDisk Manager")

    color: Theme.bgWindow

    // Backend — instantiated at top level so pages share one instance.
    // (To be moved into C++ context property in M1 for journal access.)
    UDisks2Backend {
        id: backend
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Sidebar --------------------------------------------------
        Rectangle {
            Layout.preferredWidth: 220
            color: Theme.bgSurface

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spaceL
                spacing: Theme.spaceS

                Label {
                    text: qsTr("SereneDisk")
                    font.pixelSize: Theme.fontTitle
                    font.bold: true
                    color: Theme.textPrimary
                    Layout.bottomMargin: Theme.spaceXL
                }

                Repeater {
                    model: [
                        { label: qsTr("Devices"), page: "devices" },
                        { label: qsTr("Journal"), page: "journal" }
                    ]
                    delegate: SidebarButton {
                        required property var modelData
                        text: modelData.label
                        checked: stack.currentItem?.pageName === modelData.page
                        onClicked: {
                            while (stack.depth > 1) stack.pop()
                            stack.replace(devicesPage)
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Label {
                    text: backend.available
                          ? qsTr("UDisks2 connected")
                          : qsTr("UDisks2 unreachable")
                    font.pixelSize: Theme.fontSmall
                    color: backend.available ? Theme.textMuted : Theme.warning
                }
            }
        }

        // ---- Content ----------------------------------------------------
        StackView {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            initialItem: DevicesPage { id: devicesPage }
        }
    }

    component SidebarButton: Button {
        id: ctrl
        flat: true
        font.pixelSize: Theme.fontBody
        implicitHeight: 40
        background: Rectangle {
            radius: Theme.radiusS
            color: ctrl.checked ? Theme.bgHover
                  : ctrl.hovered ? Theme.bgSurface
                                 : "transparent"
            Rectangle {
                width: 3
                height: parent.height * 0.6
                radius: 2
                anchors.verticalCenter: parent.verticalCenter
                color: ctrl.checked ? Theme.accent : "transparent"
                visible: ctrl.checked
            }
            anchors.centerIn: parent
        }
    }
}
