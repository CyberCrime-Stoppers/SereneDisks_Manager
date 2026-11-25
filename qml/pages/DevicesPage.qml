/*
 * SereneDisk Manager — DevicesPage.qml
 * Left: scrollable list of drive cards. Right: selected drive detail.
 * Read-only for M0 — look, don't touch.
 *
 * Layout hygiene rules applied here (learned the hard way):
 *   - Visibility only ever driven by explicit booleans, never by a raw
 *     `var selected` (QVariantMap → bool coercion is unreliable).
 *   - Null-guarded helper properties for everything read off the
 *     selection, so no binding can throw mid-evaluation.
 *   - Detail pane clips its contents — nothing may paint outside it.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "../components"
import SDM

Item {
    id: root
    property string pageName: "devices"

    property var selected: null

    // ---- Selection state (explicit, null-safe) --------------------------
    readonly property bool hasSelection: selected !== null
    readonly property var  sel:           hasSelection ? selected : null
    readonly property int  partitionCount: sel ? sel.partitions : 0
    readonly property var  partitionRows:  sel ? (sel.partitionDetails ?? []) : []
    // Smart map may be missing entirely when the backend exposed none.
    readonly property var  smart: sel ? (sel.smart ?? {}) : {}
    readonly property bool smartHasTemp:
        smart && smart.temperatureC !== undefined &&
        smart.temperatureC !== null
    readonly property string healthVerdict: Theme.healthVerdict(smart)
    readonly property string driveTitle: sel
        ? ((sel.vendor + " " + sel.model).trim() || sel.path.split("/").pop())
        : ""
    readonly property real allocFraction: (sel && sel.sizeBytes > 0)
        ? sel.allocatedBytes / sel.sizeBytes : 0

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceL
        spacing: Theme.spaceL

        // ---- Drive list --------------------------------------------------
        ColumnLayout {
            Layout.preferredWidth: 340
            Layout.maximumWidth: 340
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
                    onClicked: {
                        devicesBackend.refresh()
                        journalStore.record("rescan", "",
                                            qsTr("Manual device rescan triggered from the Devices page"))
                    }
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
            id: detailPane
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusL
            color: Theme.bgSurface
            clip: true   // nothing escapes the pane, ever

            // -- Empty state: nothing selected -----------------------------
            ColumnLayout {
                anchors.centerIn: parent
                visible: !root.hasSelection
                spacing: Theme.spaceS
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Select a device to inspect it")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontBig
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Device details appear here once you pick a drive.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                }
            }

            // -- Selected drive detail ---------------------------------------
            ColumnLayout {
                visible: root.hasSelection
                anchors.fill: parent
                anchors.margins: Theme.spaceXL
                spacing: Theme.spaceM

                Label {
                    text: root.driveTitle
                    font.pixelSize: Theme.fontTitle
                    font.bold: true
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Label {
                    text: root.sel
                          ? qsTr("Serial %1 · %2 · %3")
                                .arg(root.sel.serial || "—")
                                .arg(Theme.humanBytes(root.sel.sizeBytes))
                                .arg(Theme.kindLabel(root.sel.kind,
                                                     root.sel.rotational))
                          : ""
                    font.pixelSize: Theme.fontSmall
                    color: Theme.textSecondary
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }

                // ---- Health & temperature (built-in sensor) -------------
                // Calm, advisory panel: a verdict pill, live temperature,
                // power-on age, and quiet notes for anything noteworthy.
                RowLayout {
                    visible: root.hasSelection
                    spacing: Theme.spaceS
                    Layout.fillWidth: true

                    StatusPill {
                        text: root.healthVerdict === "attention"
                              ? qsTr("Needs attention")
                              : root.healthVerdict === "good"
                                    ? qsTr("Healthy")
                                    : qsTr("Health unknown")
                        tint: root.healthVerdict === "attention"
                              ? Theme.warning
                              : root.healthVerdict === "good"
                                    ? Theme.positive
                                    : Theme.textMuted
                        tooltip: root.healthVerdict === "good"
                              ? qsTr("S.M.A.R.T. reports no failing attributes or critical warnings")
                              : root.healthVerdict === "attention"
                                    ? qsTr("The drive reports a failing condition or a critical NVMe warning")
                                    : qsTr("No S.M.A.R.T. data available for this drive yet")
                    }

                    StatusPill {
                        visible: root.smartHasTemp
                        text: Theme.tempLabel(root.smart.temperatureC)
                        tint: Theme.tempColor(root.smart.temperatureC)
                        tooltip: qsTr("Live temperature from the drive's built-in sensor")
                    }

                    StatusPill {
                        visible: root.smart.powerOnHours !== undefined
                        text: qsTr("%1 h powered on")
                                  .arg(root.smart.powerOnHours.toLocaleString(
                                           Qt.locale(), "f", 0))
                        tint: Theme.textSecondary
                        tooltip: qsTr("Total power-on time reported by the drive")
                    }

                    StatusPill {
                        visible: root.smart.badSectors !== undefined
                        text: qsTr("%1 bad sectors")
                                  .arg(root.smart.badSectors)
                        tint: Theme.warning
                        tooltip: qsTr("Remapped or pending sectors reported by S.M.A.R.T.")
                    }

                    StatusPill {
                        visible: root.smart.criticalWarning !== undefined
                        text: qsTr("NVMe critical warning")
                        tint: Theme.warning
                        tooltip: qsTr("The NVMe controller flagged a critical warning state (flags %1)")
                                    .arg(root.smart.criticalWarning)
                    }

                    StatusPill {
                        visible: root.smart.selfTest !== undefined
                        text: qsTr("Self-test: %1").arg(root.smart.selfTest)
                        tint: Theme.warning
                        tooltip: qsTr("Last S.M.A.R.T. self-test did not finish cleanly")
                    }

                    Item { Layout.fillWidth: true }
                }
                ColumnLayout {
                    spacing: Theme.spaceXS
                    Layout.fillWidth: true

                    Label {
                        text: root.sel
                              ? qsTr("%1 of %2 allocated")
                                    .arg(Theme.humanBytes(
                                             root.sel.allocatedBytes))
                                    .arg(Theme.humanBytes(
                                             root.sel.sizeBytes))
                              : ""
                        font.pixelSize: Theme.fontTiny
                        color: Theme.textSecondary
                    }
                    // On-disk layout map: segments at true offsets, gaps
                    // between them read as unallocated space.
                    Label {
                        text: qsTr("On-disk layout")
                        font.pixelSize: Theme.fontTiny
                        color: Theme.textSecondary
                    }
                    PartitionMap {
                        Layout.fillWidth: true
                        partitions: root.partitionRows
                        totalBytes: root.sel ? root.sel.sizeBytes : 0
                    }
                }

                // -- Inner panel: partitions or empty state -----------------
                Rectangle {
                    id: innerPanel
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusM
                    color: Theme.bgWindow
                    clip: true

                    // Empty state: selected but no partition table
                    ColumnLayout {
                        anchors.centerIn: parent
                        visible: root.hasSelection &&
                                 root.partitionCount === 0
                        spacing: Theme.spaceS

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("No partition table detected")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontBody
                            font.bold: true
                        }
                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("The entire disk is raw space. Partitioning tools arrive with the first write-capable milestone.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            Layout.maximumWidth: 320
                        }
                    }

                    // Partitions present — list rows in physical order
                    ColumnLayout {
                        visible: root.hasSelection &&
                                 root.partitionCount > 0
                        anchors.fill: parent
                        anchors.margins: Theme.spaceL
                        spacing: Theme.spaceS

                        Label {
                            text: qsTr("Partitions (%1)")
                                      .arg(root.partitionCount)
                            font.pixelSize: Theme.fontBig
                            color: Theme.textPrimary
                        }

                        ListView {
                            id: partList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: Theme.spaceXS
                            model: root.partitionRows
                            ScrollBar.vertical: ScrollBar {}

                            delegate: RowLayout {
                                id: partRow
                                required property var modelData
                                width: partList.width
                                spacing: Theme.spaceM

                                // Partition number badge
                                Rectangle {
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 28
                                    radius: Theme.radiusS
                                    color: partRow.modelData.encrypted
                                           ? Qt.rgba(Theme.warning.r,
                                                     Theme.warning.g,
                                                     Theme.warning.b, 0.15)
                                           : Qt.rgba(Theme.accent.r,
                                                     Theme.accent.g,
                                                     Theme.accent.b, 0.15)
                                    Label {
                                        anchors.centerIn: parent
                                        text: partRow.modelData.number
                                        font.pixelSize: Theme.fontSmall
                                        font.bold: true
                                        color: partRow.modelData.encrypted
                                               ? Theme.warning
                                               : Theme.accent
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Label {
                                        Layout.fillWidth: true
                                        text: (partRow.modelData.name ||
                                               partRow.modelData.fsLabel ||
                                               partRow.modelData.fsType ||
                                               partRow.modelData.devicePath)
                                        font.pixelSize: Theme.fontBody
                                        color: Theme.textPrimary
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("%1 · %2 · %3")
                                              .arg(partRow.modelData.devicePath)
                                              .arg(Theme.humanBytes(
                                                        partRow.modelData.sizeBytes))
                                              .arg(partRow.modelData.mounted
                                                   ? qsTr("mounted")
                                                   : qsTr("not mounted"))
                                        font.pixelSize: Theme.fontTiny
                                        color: Theme.textSecondary
                                        elide: Text.ElideMiddle
                                    }
                                }

                                // Quiet per-partition capacity strip
                                UsageBar {
                                    Layout.preferredWidth: 90
                                    Layout.alignment: Qt.AlignVCenter
                                    fraction: (partRow.modelData.sizeBytes > 0 &&
                                               root.sel &&
                                               root.sel.sizeBytes > 0)
                                             ? partRow.modelData.sizeBytes /
                                               root.sel.sizeBytes
                                             : 0
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: root.sel &&
                                     root.sel.sizeBytes > 0 &&
                                     root.sel.allocatedBytes <
                                     root.sel.sizeBytes
                            wrapMode: Text.WrapAnywhere
                            text: root.sel
                                  ? qsTr("%1 unallocated")
                                        .arg(Theme.humanBytes(
                                                 root.sel.sizeBytes -
                                                 root.sel.allocatedBytes))
                                  : ""
                            font.pixelSize: Theme.fontTiny
                            color: Theme.textMuted
                        }
                    }
                }
            }
        }
    }
}
