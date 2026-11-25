/*
 * SereneDisk Manager — Main.qml
 * Application shell: sidebar navigation + content stack.
 * The backend arrives as the "devicesBackend" context property from main.cpp.
 *
 * Pages live INSIDE a StackLayout so exactly one is visible at a time.
 * (Declaring them at window level made both render stacked at 0,0.)
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import SDM
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

    // 0 = Devices, 1 = Journal
    readonly property var pages: [qsTr("Devices"), qsTr("Journal")]

    function navigate(index) {
        if (stack.currentIndex === index) return
        stack.currentIndex = index
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Sidebar --------------------------------------------------
        Rectangle {
            id: sidebar
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            color: Theme.bgSurface

            // Subtle right edge so the sidebar reads as its own panel
            Rectangle {
                width: 1
                height: parent.height
                anchors.right: parent.right
                color: Qt.rgba(Theme.textMuted.r, Theme.textMuted.g,
                               Theme.textMuted.b, 0.15)
            }

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
                    model: root.pages
                    delegate: SidebarButton {
                        required property var modelData
                        required property int index
                        text: modelData
                        checked: stack.currentIndex === index
                        onClicked: root.navigate(index)
                    }
                }

                Item { Layout.fillHeight: true }

                Label {
                    Layout.fillWidth: true
                    text: devicesBackend.available
                          ? qsTr("UDisks2 connected")
                          : qsTr("UDisks2 unreachable")
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                    color: devicesBackend.available
                          ? Theme.textMuted : Theme.warning
                }
            }
        }

        // ---- Content ----------------------------------------------------
        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: 0

            DevicesPage {}
            JournalPage {}
        }
    }

    component SidebarButton: Button {
        id: ctrl
        flat: true
        font.pixelSize: Theme.fontBody
        leftPadding: Theme.spaceM
        implicitHeight: 40
        Layout.fillWidth: true
        background: Rectangle {
            radius: Theme.radiusS
            color: ctrl.checked ? Theme.bgHover
                  : ctrl.hovered ? Theme.bgElevated
                                 : "transparent"
            Rectangle {
                width: 3
                height: parent.height * 0.6
                radius: 2
                anchors.left: parent.left
                anchors.leftMargin: 2
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.accent
                visible: ctrl.checked
            }
        }
    }
}
