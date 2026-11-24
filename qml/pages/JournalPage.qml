/*
 * SereneDisk Manager — JournalPage.qml
 * M0 placeholder: lists session operations once the journal engine
 * (operationFinished -> Journal store) lands in M1/M2.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

Item {
    id: root
    property string pageName: "journal"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceXL
        spacing: Theme.spaceM

        Label {
            text: qsTr("Journal")
            font.pixelSize: Theme.fontTitle
            font.bold: true
            color: Theme.textPrimary
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Every operation SereneDisk Manager performs — mounts, formats, resizes, repairs — will be recorded here with a full parameter audit trail.\n\nThe journal engine ships with the first write-capable milestone (M2). Until then, SereneDisk Manager is read-only.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontBody
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusL
            color: Theme.bgSurface

            Label {
                anchors.centerIn: parent
                text: qsTr("No operations recorded yet")
                color: Theme.textMuted
            }
        }
    }
}
