pragma Singleton
import QtQuick

QtObject {
    property bool dark: true
    readonly property color background: dark ? "#171717" : "#f6f6f5"
    readonly property color surface: dark ? "#202020" : "#ffffff"
    readonly property color elevated: dark ? "#292929" : "#ededeb"
    readonly property color hover: dark ? "#333333" : "#e6e6e3"
    readonly property color border: dark ? "#343434" : "#dededb"
    readonly property color ink: dark ? "#eeeeec" : "#202020"
    readonly property color secondary: dark ? "#a1a19d" : "#666663"
    readonly property color muted: dark ? "#70706d" : "#888883"
    readonly property color viewer: dark ? "#111111" : "#e8e8e5"
    readonly property color titleBar: dark ? "#1e2225" : "#ededeb"
    readonly property color menuBar: dark ? "#2b2b2d" : "#ededeb"
    readonly property color accent: dark ? "#a8d5c2" : "#33735b"
    readonly property color accentSurface: dark ? "#273d34" : "#dceee5"
    readonly property color warning: dark ? "#e6bf79" : "#986015"
    readonly property color warningSurface: dark ? "#362f22" : "#fff1d8"
    readonly property color transparent: "transparent"
    readonly property var bookmarkColors: ["#e6bf79", "#a8d5c2", "#aebbe8", "#d3a7c4"]
    readonly property string font: "Segoe UI"
    readonly property string monoFont: "Consolas"
    readonly property int textSmall: 11
    readonly property int textBody: 13
    readonly property int textTitle: 16
    readonly property int textHero: 25
    readonly property int spaceXs: 4
    readonly property int spaceSm: 8
    readonly property int spaceMd: 12
    readonly property int spaceLg: 20
    readonly property int spaceXl: 32
    readonly property int radiusSm: 6
    readonly property int radiusMd: 10
    readonly property int radiusLg: 16
    readonly property int controlHeight: 34
    readonly property int iconSize: 18
    readonly property int toolbarHeight: 62
    readonly property int menuBarHeight: 24
    readonly property int menuItemHeight: 30
    readonly property int menuTextSize: 12
    readonly property int menuWidth: 244
    readonly property int bookmarkRailWidth: 72
    readonly property int sidebarWidth: 220
    readonly property int bookmarkRowHeight: 38
    readonly property int timelineHeight: 90
    readonly property int transitionMs: 110
    readonly property int defaultWidth: 1160
    readonly property int defaultHeight: 800
    readonly property int minimumWidth: 860
    readonly property int minimumHeight: 540
}
