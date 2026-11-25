/*
 * SereneDisk Manager — JournalPage.qml
 * Append-only operation audit trail, backed by journalStore
 * (QAbstractListModel from C++). M1 shows real entries (manual rescans);
 * M2 write operations (mount/format/resize/…) record here automatically.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import SDM

Item {
    id: root
    property string pageName: "journal"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceXL
        spacing: Theme.spaceM

        RowLayout {
            spacing: Theme.spaceS
            Layout.fillWidth: true

            Label {
                text: qsTr("Journal")
                font.pixelSize: Theme.fontTitle
                font.bold: true
                color: Theme.textPrimary
            }
            Item { Layout.fillWidth: true }

            Button {
                flat: true
                visible: journalStore.count > 0
                text: qsTr("Clear trail")
                onClicked: journalStore.clear()
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: journalStore.count > 0
                  ? qsTr("%1 operation(s) recorded. The trail is append-only — entries are never edited or removed except by clearing.").arg(journalStore.count)
                  : qsTr("Every operation SereneDisk Manager performs — mounts, formats, resizes, repairs — will be recorded here with a full parameter audit trail.\n\nThe journal engine ships with the first write-capable milestone (M2). Until then, SereneDisk Manager is read-only.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontBody
        }

        // ---- Entries -----------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusL
            color: Theme.bgSurface
            clip: true

            ColumnLayout {
                anchors.centerIn: parent
                visible: journalStore.count === 0
                spacing: Theme.spaceS
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("No operations recorded yet")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontBody
                    font.bold: true
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Hit ⟳ on the Devices page — rescan operations land here.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                }
            }

            ListView {
                id: entryList
                visible: journalStore.count > 0
                anchors.fill: parent
                anchors.margins: Theme.spaceL
                clip: true
                spacing: Theme.spaceS
                model: journalStore
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: entryCard
                    width: entryList.width
                    height: entryLayout.implicitHeight + Theme.spaceM * 2
                    radius: Theme.radiusM
                    color: Theme.bgWindow

                    RowLayout {
                        id: entryLayout
                        anchors.fill: parent
                        anchors.margins: Theme.spaceM
                        spacing: Theme.spaceM

                        // Outcome dot — calm green for success, quiet red
                        // for failure; nothing louder than a dot.
                        Rectangle {
                            Layout.preferredWidth: 10
                            Layout.preferredHeight: 10
                            radius: 5
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 5
                            color: model.outcome === "failure"
                                   ? Theme.danger : Theme.positive
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spaceS

                                Label {
                                    text: model.opType.toUpperCase()
                                    font.pixelSize: Theme.fontTiny
                                    font.bold: true
                                    // Quiet type chip via letterspacing feel
                                    color: model.outcome === "failure"
                                           ? Theme.danger : Theme.accent
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: model.device
                                    font.pixelSize: Theme.fontSmall
                                    color: Theme.textSecondary
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    // Local time display of the UTC stamp
                                    text: Qt.formatDateTime(model.timestamp,
                                                            "yyyy-MM-dd hh:mm:ss")
                                    font.pixelSize: Theme.fontTiny
                                    color: Theme.textMuted
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                text: model.detail
                                font.pixelSize: Theme.fontBody
                                color: Theme.textPrimary
                                wrapMode: Text.WrapAnywhere
                            }
                        }
                    }
                }
            }
        }
    }
}
