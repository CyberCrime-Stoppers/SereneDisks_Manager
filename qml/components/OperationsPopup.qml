/*
 * SereneDisk Manager — OperationsPopup.qml
 * The "disk surgery" popup: format, resize, create, and the HDD
 * refragment tool (SSDs get a TRIM pass instead — see Operations.cpp).
 *
 * Views (StackLayout indices):
 *   0 — tool chooser (format / resize / refrag)
 *   1 — format form
 *   2 — resize form
 *   3 — refragment briefing
 *   4 — backend confirmation (confirmRequired)
 *   5 — busy / result banner
 *   6 — create-partition form (opened via openCreate())
 *
 * Flow follows the backend contract in Operations.h:
 *   1. The user fills a tool form and presses "Continue".
 *   2. The corresponding Q_INVOKABLE is called on storageOps.
 *   3. storageOps validates and emits confirmRequired(title, body, op).
 *   4. This popup switches to its confirmation view; "Do it" calls
 *      storageOps.execute(op). Refusals and outcomes surface through
 *      operationFinished and are shown in the popup's result banner.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import SDM
import "."

Dialog {
    id: popup

    // The partition this popup operates on; set by openFor().
    property var target: null
    readonly property string targetPath:
        target ? (target.devicePath || "") : ""
    readonly property bool isRotational:
        target ? (target.rotational === true) : false

    modal: true
    anchors.centerIn: parent
    width: 520
    padding: Theme.spaceL
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: Rectangle {
        color: Theme.bgElevated
        radius: Theme.radiusL
        border.width: 1
        border.color: Theme.border
    }

    // ---- View state ------------------------------------------------------
    property int view: 0
    property string pendingTitle: ""
    property string pendingBody: ""
    property var    pendingOp: null
    property string resultText: ""
    property bool   resultOk: false

    // Create-partition context: the free-space span clicked on the map.
    property var    createDisk: null     // whole-disk map (has .path)
    property real   createOffset: 0
    property real   createSize: 0

    function openFor(partition) {
        target = partition
        view = 0
        resultText = ""
        open()
    }

    // Open directly on the create-partition form for a free-space span.
    function openCreate(disk, offsetBytes, sizeBytes) {
        createDisk = disk
        createOffset = offsetBytes
        createSize = sizeBytes
        target = null
        view = 6
        resultText = ""
        open()
    }

    // Backend interactions -------------------------------------------------
    Connections {
        target: storageOps
        function onConfirmRequired(title, body, op) {
            popup.pendingTitle = title
            popup.pendingBody = body
            popup.pendingOp = op
            popup.view = 4
        }
        function onOperationStarted(kind, tgt) {
            popup.resultText = qsTr("Running %1 on %2 …").arg(kind, tgt)
            popup.resultOk = true
            popup.view = 5
        }
        function onOperationFinished(kind, tgt, success, message) {
            popup.resultText = success
                  ? qsTr("%1 finished: %2").arg(kind, message || qsTr("done"))
                  : qsTr("%1 failed: %2").arg(kind, message || qsTr("unknown error"))
            popup.resultOk = success
            popup.view = 5
            // Rescan so the device list reflects reality.
            devicesBackend.refresh()
        }
    }

    header: ColumnLayout {
        spacing: Theme.spaceXS

        Label {
            Layout.fillWidth: true
            text: popup.view === 6
                  ? qsTr("Create partition")
                  : popup.targetPath
                    ? qsTr("Manage %1").arg(popup.targetPath)
                    : qsTr("Manage partition")
            font.pixelSize: Theme.fontBig
            font.bold: true
            color: Theme.textPrimary
            elide: Text.ElideRight
        }
        Label {
            Layout.fillWidth: true
            visible: popup.view === 6 && popup.createDisk !== null
            text: popup.createDisk
                  ? qsTr("Free space on %1 · %2 available")
                        .arg(popup.createDisk.path.split("/").pop())
                        .arg(Theme.humanBytes(popup.createSize))
                  : ""
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
        Label {
            Layout.fillWidth: true
            visible: popup.view !== 6 && popup.targetPath !== ""
            text: popup.target
                  ? qsTr("%1 · %2 · %3")
                        .arg(popup.target.fsType || "?")
                        .arg(Theme.humanBytes(popup.target.sizeBytes || 0))
                        .arg(popup.isRotational
                             ? qsTr("rotating platters")
                             : qsTr("solid state"))
                  : ""
            font.pixelSize: Theme.fontSmall
            color: Theme.textSecondary
            elide: Text.ElideRight
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Back")
            flat: true
            visible: popup.view >= 1 && popup.view <= 3
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: popup.view = 0
        }
        Button {
            text: qsTr("Cancel")
            flat: true
            visible: popup.view === 6
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: popup.close()
        }
        Button {
            text: qsTr("Close")
            flat: true
            visible: popup.view === 0 || popup.view === 5
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: popup.close()
        }
        Button {
            id: goBtn
            text: qsTr("Continue")
            highlighted: true
            visible: (popup.view >= 1 && popup.view <= 3) ||
                     popup.view === 6
            onClicked: popup.submit(popup.view)
        }
        Button {
            text: qsTr("Cancel")
            flat: true
            visible: popup.view === 4
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: {
                popup.pendingOp = null
                popup.view = 5
                popup.resultText = qsTr("Cancelled — nothing was changed.")
                popup.resultOk = true
            }
        }
        Button {
            text: qsTr("Do it")
            highlighted: true
            visible: popup.view === 4
            onClicked: {
                if (popup.pendingOp) storageOps.execute(popup.pendingOp)
                popup.pendingOp = null
            }
        }
    }

    // Submit one of the tool forms → backend entry points.
    function submit(v) {
        if (v === 6) {
            if (!popup.createDisk) return
            storageOps.create(popup.createDisk.path,
                              popup.createOffset,
                              createPane.newSizeBytes(),
                              createPane.fsType,
                              createPane.label)
            return
        }
        if (popup.targetPath === "")
            return
        if (v === 1) {
            storageOps.format(popup.targetPath,
                              formatPane.fsType,
                              formatPane.label)
        } else if (v === 2) {
            storageOps.resize(popup.targetPath,
                              resizePane.newSizeBytes())
        } else if (v === 3) {
            storageOps.refrag(popup.targetPath)
        }
    }

    contentItem: StackLayout {
        currentIndex: popup.view

        // ---- View 0: tool chooser -----------------------------------------
        ColumnLayout {
            spacing: Theme.spaceS

            Label {
                text: qsTr("What would you like to do?")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
            }

            component ToolButton2: AbstractButton {
                id: tb
                Layout.fillWidth: true
                implicitHeight: 52
                contentItem: ColumnLayout {
                    spacing: 0
                    Label {
                        text: tb.text
                        font.pixelSize: Theme.fontBody
                        font.bold: true
                        color: Theme.textPrimary
                    }
                    Label {
                        text: tb.sub
                        font.pixelSize: Theme.fontTiny
                        color: Theme.textMuted
                    }
                }
                property string sub: ""
                background: Rectangle {
                    radius: Theme.radiusM
                    color: tb.hovered ? Theme.bgHover : Theme.bgSurface
                    border.width: 1
                    border.color: Theme.border
                }
            }

            ToolButton2 {
                text: qsTr("Format")
                sub: qsTr("Write a fresh filesystem — everything currently on the partition is lost.")
                onClicked: popup.view = 1
            }
            ToolButton2 {
                text: qsTr("Resize")
                sub: qsTr("Grow or shrink the partition. Growing is usually safe online; shrinking may need unmounting.")
                onClicked: popup.view = 2
            }
            ToolButton2 {
                text: qsTr("Refragment / TRIM")
                sub: popup.isRotational
                      ? qsTr("Defragment this HDD's filesystem with its native tool (e4defrag, xfs_fsr, btrfs …).")
                      : qsTr("SSDs don't benefit from relocation — a TRIM pass will reclaim freed blocks instead.")
                onClicked: popup.view = 3
            }
        }

        // ---- View 1: format form -------------------------------------------
        ColumnLayout {
            id: formatPane
            property string fsType: "ext4"
            property string label: ""

            Label {
                text: qsTr("New filesystem")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
            }
            ComboBox {
                id: fsCombo
                Layout.fillWidth: true
                model: ["ext4", "xfs", "btrfs", "f2fs", "swap"]
                onActivated: formatPane.fsType = currentText
            }
            Label {
                text: qsTr("Volume label (optional)")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
            }
            TextField {
                id: labelField
                Layout.fillWidth: true
                placeholderText: qsTr("e.g. data-backup")
                onTextChanged: formatPane.label = text.trim()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.warning
                font.pixelSize: Theme.fontSmall
                text: qsTr("Formatting overwrites every byte of the partition's old content. The backend will refuse if it hosts /, /boot, /home or /var.")
            }
        }

        // ---- View 2: resize form -------------------------------------------
        ColumnLayout {
            id: resizePane
            spacing: Theme.spaceS

            // Operates in whole GiB steps — calm granularity for humans.
            readonly property real sizeGiB: popup.target
                  ? (popup.target.sizeBytes || 0) / (1024*1024*1024) : 0
            function newSizeBytes() {
                return Math.max(1, Math.round(sizeSlider.value)) * 1024*1024*1024
            }

            Label {
                text: popup.target
                      ? qsTr("Current size: %1")
                            .arg(Theme.humanBytes(popup.target.sizeBytes || 0))
                      : ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
            }
            Slider {
                id: sizeSlider
                Layout.fillWidth: true
                from: 1
                to: Math.max(2, Math.ceil(resizePane.sizeGiB * 4)) // allow growing into free space
                stepSize: 1
                value: resizePane.sizeGiB
                snapMode: Slider.SnapAlways
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("New size: %1 GiB")
                          .arg(Math.round(sizeSlider.value))
                font.pixelSize: Theme.fontBody
                font.bold: true
                color: Theme.accent
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: Theme.fontTiny
                text: qsTr("Shrinking usually requires the volume to be unmounted; growing is safe online on ext4/xfs/btrfs. UDisks2 refuses rather than corrupting when an online shrink is unsafe.")
            }
        }

        // ---- View 3: refragment briefing -----------------------------------
        ColumnLayout {
            spacing: Theme.spaceS

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                text: popup.isRotational
                      ? qsTr("This HDD will be defragmented with the native tool for its filesystem: e4defrag for ext4, xfs_fsr for XFS, or btrfs defragment. The command runs through pkexec so the OS shows you exactly what executes.")
                      : qsTr("This is a solid-state drive. Moving blocks around gains nothing and wears the cells, so a TRIM pass (fstrim) will run instead — it tells the drive which blocks are free again.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSmall
                color: Theme.textMuted
                text: qsTr("Defragmenting a live root/home is safe; the backend allows it where destructive operations are refused.")
            }
        }

        // ---- View 4: backend confirmation ----------------------------------
        ColumnLayout {
            spacing: Theme.spaceM

            Label {
                text: popup.pendingTitle
                font.pixelSize: Theme.fontBig
                font.bold: true
                color: Theme.warning
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: popup.pendingBody
                font.pixelSize: Theme.fontBody
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        // ---- View 5: busy / result -----------------------------------------
        ColumnLayout {
            spacing: Theme.spaceS

            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                running: popup.view === 5
                visible: popup.view === 5 &&
                         popup.resultText.indexOf("…") !== -1
            }
            Label {
                Layout.fillWidth: true
                text: popup.resultText
                color: popup.resultOk ? Theme.positive : Theme.danger
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontBody
            }
        }

        // ---- View 6: create-partition form ----------------------------------
        ColumnLayout {
            id: createPane
            spacing: Theme.spaceS

            property string fsType: "ext4"
            property string label: ""

            // Whole-GiB slider capped at the clicked free span. 1 GiB floor
            // keeps the partition from being accidentally microscopic.
            function newSizeBytes() {
                return Math.max(1, Math.round(createSizeSlider.value)) *
                       1024 * 1024 * 1024
            }

            readonly property real maxGiB:
                Math.max(1, Math.floor(popup.createSize /
                                       (1024 * 1024 * 1024)))

            Label {
                text: qsTr("Filesystem")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
            }
            ComboBox {
                id: createFsCombo
                Layout.fillWidth: true
                model: ["ext4", "xfs", "btrfs", "f2fs", "swap"]
                onActivated: createPane.fsType = currentText
            }
            Label {
                text: qsTr("Volume label (optional)")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
            }
            TextField {
                id: createLabelField
                Layout.fillWidth: true
                placeholderText: qsTr("e.g. media")
                onTextChanged: createPane.label = text.trim()
            }
            Label {
                text: qsTr("Size")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSmall
            }
            Slider {
                id: createSizeSlider
                Layout.fillWidth: true
                from: 1
                to: createPane.maxGiB
                stepSize: 1
                value: to
                snapMode: Slider.SnapAlways
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("New size: %1 GiB")
                          .arg(Math.round(createSizeSlider.value))
                font.pixelSize: Theme.fontBody
                font.bold: true
                color: Theme.accent
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: Theme.fontTiny
                text: qsTr("The partition will be created inside the free space you clicked, aligned to UDisks2 defaults.")
            }
        }
    }
}
