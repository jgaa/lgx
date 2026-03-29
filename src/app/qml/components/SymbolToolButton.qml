import QtQuick
import QtQuick.Controls

ToolButton {
    id: root

    property color fgColor: "#2c2823"
    property color bgColor: "#e2ddd3"
    property color checkedBgColor: "#c7d7e8"
    property string symbol: "help"
    property int symbolPixelSize: 20

    implicitWidth: 34
    implicitHeight: 34
    padding: 0
    hoverEnabled: true

    contentItem: Label {
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: root.symbol
        font.family: "Material Symbols Outlined"
        font.pixelSize: root.symbolPixelSize
        color: root.enabled ? root.fgColor : Qt.rgba(root.fgColor.r, root.fgColor.g, root.fgColor.b, 0.45)
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
