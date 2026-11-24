/*
 * SereneDisk Manager — Main.qml
 * Application shell: sidebar navigation + content stack.
 * The backend arrives as the "devicesBackend" context property from main.cpp.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "pages"

ApplicationWindow {
    id: root
    width: 1080
    height: 700
    minimumWidth: 860
    minimumHeight: 560
    visible: true
    title: qsTr("SereneDisk Manager")

    color: Theme.bgWindow

    // Instantiate both pages once; navigate by swapping the stack item.
    DevicesPage { id: devicesPage }
    JournalPage { id: journalPage }

    function navigate(page) {
        if (stack.currentItem === page) return
        stack.replace(page)
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
                        { label: qsTr("Devices"), page: devicesPage },
                        { label: qsTr("Journal"), page: journalPage }
                    ]
                    delegate: SidebarButton {
                        required property var modelData
                        text: modelData.label
                        checked: stack.currentItem === modelData.page
                        onClicked: root.navigate(modelData.page)
                    }
                }

                Item { Layout.fillHeight: true }

                Label {
                    text: devicesBackend.available
                          ? qsTr("UDisks2 connected")
                          : qsTr("UDisks2 unreachable")
                    font.pixelSize: Theme.fontSmall
                    color: devicesBackend.available ? Theme.textMuted : Theme.warning
                }
            }
        }

        // ---- Content ----------------------------------------------------
        StackView {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            initialItem: devicesPage
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
