import QtQuick
import QtQuick.Controls

SymbolToolButton {
    id: root

    property var hoverTarget: null

    anchors.top: parent.top
    anchors.right: parent.right
    anchors.margins: 2
    z: 2
    visible: hovered || down || (!!hoverTarget && hoverTarget.containsMouse)
    symbol: "menu"
    symbolPixelSize: 14
    implicitWidth: 18
    implicitHeight: 18
    bgColor: "#00000000"
    accessibleName: qsTr("View menu")
}
