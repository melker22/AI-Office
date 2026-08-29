pragma Singleton
import QtQuick

QtObject {
    id: t
    property bool dark: false

    readonly property string uiFont: "IBM Plex Sans"
    readonly property string serifFont: "Source Serif 4"

    // Tinta & Papel — UX Pilot tokens
    readonly property color paper: dark ? "#1D1916" : "#FBF7F0"
    readonly property color paper100: dark ? "#EFE7DA" : "#FDFBF7"
    readonly property color workspace: dark ? "#0E0C0A" : "#E4DDD2"
    readonly property color ink: dark ? "#EFE7DA" : "#2A2420"
    readonly property color inkMuted: dark ? "#8C8272" : "#6B5F51"
    readonly property color inkSoft: dark ? "#B3A895" : "#8A7D6C"
    readonly property color accent: dark ? "#D68A44" : "#B5651D"
    readonly property color accentHover: dark ? "#C4762F" : "#9C5217"
    readonly property color accent50: dark ? "#2E2115" : "#FBF0E6"
    readonly property color accent100: dark ? "#3A2818" : "#F5DFC6"
    readonly property color hairline: dark ? "#332C26" : "#E4DDD2"
    readonly property color hairlineStrong: dark ? "#4B4239" : "#D2C8B8"
    readonly property color surface: dark ? "#211C18" : "#FAF8F5"
    readonly property color surfaceAlt: dark ? "#161310" : "#F1EDE6"
    readonly property color titleBar: dark ? "#161310" : "#362F28"
    readonly property color titleText: "#F1EDE6"
    readonly property color titleMuted: dark ? "#8C8272" : "#AFA391"
    readonly property color hover: dark ? "#332C26" : "#F1EDE6"
    readonly property color icon: dark ? "#B3A895" : "#6B5F51"
    readonly property color iconOnDark: "#D6CCBB"
    readonly property color success: dark ? "#5FA772" : "#3F7D4F"
    readonly property color warning: dark ? "#D6A24C" : "#B8862E"
    readonly property color destructive: dark ? "#D6644A" : "#B4402A"
    readonly property color accentInk: "#FFFFFF"
    readonly property color toastBg: "#2A2420"
    readonly property color panelShadow: dark ? "#000000" : "#2A2420"
}
