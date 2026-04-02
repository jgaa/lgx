import QtQuick
import QtQuick.Controls

ToolButton {
    id: root

    property color fgColor: "#2c2823"
    property color bgColor: "#e2ddd3"
    property color checkedBgColor: "#c7d7e8"
    property url iconSource: ""
    property string symbol: "help"
    property string accessibleName: ""
    property int symbolPixelSize: 18
    readonly property int effectiveSymbolPixelSize: Math.max(12, Math.min(symbolPixelSize, Math.min(width, height) - 10))

    implicitWidth: 30
    implicitHeight: 30
    padding: 0
    hoverEnabled: true

    Accessible.name: root.accessibleName

    contentItem: Item {
        Image {
            anchors.centerIn: parent
            visible: root.iconSource.toString().length > 0
            source: root.iconSource
            width: root.effectiveSymbolPixelSize
            height: root.effectiveSymbolPixelSize
            sourceSize.width: root.effectiveSymbolPixelSize
            sourceSize.height: root.effectiveSymbolPixelSize
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            opacity: root.enabled ? 1.0 : 0.45
        }

        Label {
            anchors.fill: parent
            visible: root.iconSource.toString().length === 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: root.symbol
            font.family: "Material Symbols Outlined"
            font.pixelSize: root.effectiveSymbolPixelSize
            color: root.enabled ? root.fgColor : Qt.rgba(root.fgColor.r, root.fgColor.g, root.fgColor.b, 0.45)
        }
    }

    background: Rectangle {
        radius: 8
        color: {
            if (!root.enabled) {
                return Qt.rgba(root.bgColor.r, root.bgColor.g, root.bgColor.b, 0.45)
            }

            if (root.checked) {
                return root.down ? Qt.darker(root.checkedBgColor, 1.1) : root.checkedBgColor
            }

            if (root.down) {
                return Qt.darker(root.bgColor, 1.18)
            }

            if (root.hovered) {
                return Qt.lighter(root.bgColor, 1.08)
            }

            return root.bgColor
        }
        border.width: root.visualFocus ? 2 : 0
        border.color: "#7f9bb8"
    }
}
