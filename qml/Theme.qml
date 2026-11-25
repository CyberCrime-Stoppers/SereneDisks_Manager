/*
 * SereneDisk Manager — Theme.qml
 * The Serene Theme: dark-mode-first, flat, quiet. Single source of truth
 * for palette, spacing, and typography across the whole app.
 *
 * Registered as a singleton "Theme" via qt_add_qml_module + qmldir.
 */
pragma Singleton
import QtQuick

QtObject {
    id: root

    // ---- Palette (dark-first) -------------------------------------------
    readonly property color bgWindow:   "#0f1015"  // deep charcoal-violet
    readonly property color bgSurface:  "#171922"  // cards, panels
    readonly property color bgElevated: "#1e2130"  // popovers, menus
    readonly property color bgHover:    "#262a3b"

    readonly property color textPrimary:   "#e8eaf2"
    readonly property color textSecondary: "#9aa0b4"
    readonly property color textMuted:     "#6b7186"

    readonly property color accent:        "#6d4aff"  // sereneOS signature violet
    readonly property color accentHover:   "#7f5fff"
    readonly property color accentPressed: "#5c3ce6"

    // Calm status colors — never alarm-red walls.
    readonly property color positive: "#3ec98a"  // healthy, success
    readonly property color warning:  "#e6b455"  // gentle amber
    readonly property color danger:   "#e06c75"  // reserved, quiet red

    readonly property color border:        "#2a2e42"
    readonly property color borderFocused: accent

    // ---- Spacing (generous, calm rhythm) --------------------------------
    readonly property int spaceXS: 4
    readonly property int spaceS:  8
    readonly property int spaceM:  12
    readonly property int spaceL:  20
    readonly property int spaceXL: 32

    // ---- Typography -------------------------------------------------------
    readonly property int fontTiny:   10
    readonly property int fontSmall:  12
    readonly property int fontBody:   14
    readonly property int fontBig:   17
    readonly property int fontTitle: 22

    // ---- Radii (soft, flat, friendly) -------------------------------------
    readonly property int radiusS: 6
    readonly property int radiusM: 10
    readonly property int radiusL: 16

    // ---- Helpers ----------------------------------------------------------
    readonly property string fontFamily: "Inter"

    // Human-readable byte formatting used across all views.
    function humanBytes(bytes) {
        if (bytes === 0 || isNaN(bytes)) return qsTr("—")
        const units = ["B", "KiB", "MiB", "GiB", "TiB"]
        let v = bytes
        let u = 0
        while (v >= 1024 && u < units.length - 1) { v /= 1024; ++u }
        const prec = v >= 100 ? 0 : v >= 10 ? 1 : 2
        return `${v.toFixed(prec)} ${units[u]}`
    }

    // Short, calm status text for a disk kind.
    function kindLabel(kind, rotational) {
        switch (kind) {
            case "ssd":       return qsTr("Solid State")
            case "hdd":       return qsTr("Hard Disk")
            case "removable": return qsTr("Removable")
            default:          return qsTr("Disk")
        }
    }

    // ---- Thermal bands (calm, advisory — never alarm-red) ---------------
    // Thresholds follow common vendor guidance: NVMe drives run hotter by
    // design (idle ~40 °C is normal), so the bands are generous, and any
    // reading above 70 °C simply reads as "running hot", not "danger".
    readonly property double tempCoolMax:  40   // ≤ this: cool & happy
    readonly property double tempWarmMax:  55   // ≤ this: nominal
    readonly property double tempHotMax:   70   // ≤ this: warm, worth noting

    function tempBand(tempC) {
        if (tempC === undefined || tempC === null || isNaN(tempC))
            return "unknown"
        if (tempC <= tempCoolMax) return "cool"
        if (tempC <= tempWarmMax) return "nominal"
        if (tempC <= tempHotMax)  return "warm"
        return "hot"
    }

    function tempColor(tempC) {
        switch (tempBand(tempC)) {
            case "cool":    return positive
            case "nominal": return textSecondary
            case "warm":    return warning
            case "hot":     return warning
            default:        return textMuted
        }
    }

    // Calm one-liner for a temperature band, e.g. "41 °C · warm".
    function tempLabel(tempC) {
        if (tempBand(tempC) === "unknown")
            return qsTr("temp n/a")
        return qsTr("%1 °C · %2")
                  .arg(Math.round(tempC))
                  .arg({
                      "cool":    qsTr("cool"),
                      "nominal": qsTr("nominal"),
                      "warm":    qsTr("warm"),
                      "hot":     qsTr("running hot")
                  }[tempBand(tempC)])
    }

    // Overall drive health verdict from the backend's smart map:
    // "failing" / criticalWarning wins over anything else; otherwise a
    // real temperature reading counts as good health signal.
    function healthVerdict(smart) {
        if (!smart) return "unknown"
        if (smart.failing === true || smart.criticalWarning)
            return "attention"
        if (smart.temperatureC !== undefined &&
                smart.temperatureC !== null)
            return "good"
        return "unknown"
    }
}
