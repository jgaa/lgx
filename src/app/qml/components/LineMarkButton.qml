import QtQuick
import QtQuick.Controls

AbstractButton {
    id: root

    property bool marked: false
    property color markColor: "#2f855a"
    property color inactiveColor: "#ffffff"

    implicitWidth: 16
    implicitHeight: implicitWidth
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    activeFocusOnTab: false

    contentItem: Label {
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: "circle"
        font.family: "Material Symbols Outlined"
        font.pixelSize: Math.max(1, root.height)
        color: root.marked ? root.markColor : root.inactiveColor
    }

    background: Rectangle {
        color: {
            if (root.down) {
                return Qt.rgba(1, 1, 1, 0.12)
            }

            if (root.hovered) {
                return Qt.rgba(1, 1, 1, 0.08)
            }

            return "transparent"
        }
    }
}
