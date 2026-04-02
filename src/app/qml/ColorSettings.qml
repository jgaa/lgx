import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    anchors.fill: parent
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    property var originalLogLevelStyles: []
    readonly property real rowSpacing: 12
    readonly property real levelColumnWidth: Math.min(120, Math.max(88, availableWidth * 0.18))
    readonly property var colorChoices: [
        { name: qsTr("White"), value: "white" },
        { name: qsTr("Ivory"), value: "ivory" },
        { name: qsTr("Beige"), value: "beige" },
        { name: qsTr("Light Yellow"), value: "lightyellow" },
        { name: qsTr("Yellow"), value: "yellow" },
        { name: qsTr("Amber"), value: "#ffbf00" },
        { name: qsTr("Orange"), value: "orange" },
        { name: qsTr("Orange Red"), value: "orangered" },
        { name: qsTr("Coral"), value: "coral" },
        { name: qsTr("Red"), value: "red" },
        { name: qsTr("Crimson"), value: "crimson" },
        { name: qsTr("Burgundy"), value: "#800020" },
        { name: qsTr("Magenta"), value: "magenta" },
        { name: qsTr("Dark Magenta"), value: "darkmagenta" },
        { name: qsTr("Maroon"), value: "maroon" },
        { name: qsTr("Brown"), value: "brown" },
        { name: qsTr("Mint"), value: "mintcream" },
        { name: qsTr("Green"), value: "green" },
        { name: qsTr("Sea Green"), value: "seagreen" },
        { name: qsTr("Olive"), value: "olive" },
        { name: qsTr("Dark Olive"), value: "darkolivegreen" },
        { name: qsTr("Teal"), value: "teal" },
        { name: qsTr("Cyan"), value: "cyan" },
        { name: qsTr("Sky Blue"), value: "skyblue" },
        { name: qsTr("Blue"), value: "blue" },
        { name: qsTr("Royal Blue"), value: "royalblue" },
        { name: qsTr("Cornflower Blue"), value: "cornflowerblue" },
        { name: qsTr("Navy"), value: "navy" },
        { name: qsTr("Midnight Blue"), value: "midnightblue" },
        { name: qsTr("Dark Slate Blue"), value: "darkslategray" },
        { name: qsTr("Gold"), value: "gold" },
        { name: qsTr("Dark Golden Rod"), value: "darkgoldenrod" },
        { name: qsTr("Purple"), value: "purple" },
        { name: qsTr("Indigo"), value: "indigo" },
        { name: qsTr("Violet"), value: "violet" },
        { name: qsTr("Pink"), value: "pink" },
        { name: qsTr("Silver"), value: "silver" },
        { name: qsTr("Gray"), value: "gray" },
        { name: qsTr("Slate"), value: "slategray" },
        { name: qsTr("Charcoal"), value: "#36454f" },
        { name: qsTr("Black"), value: "black" }
    ]

    function cloneLogLevelStyles() {
        return UiSettings.logLevelStyles.map(function(style) {
            return {
                level: style.level,
                foregroundColor: style.foregroundColor,
                backgroundColor: style.backgroundColor
            }
        })
    }

    function colorChoiceIndex(value) {
        const normalizedValue = value ? value.toString().toLowerCase() : ""
        for (let index = 0; index < colorChoices.length; ++index) {
            const candidateValue = colorChoices[index].value.toLowerCase()
            if (candidateValue === normalizedValue || Qt.colorEqual(colorChoices[index].value, value)) {
                return index
            }
        }
        return -1
    }

    function colorChoiceValue(index, fallback) {
        return index >= 0 && index < colorChoices.length ? colorChoices[index].value : fallback
    }

    function colorChoiceName(index, fallback) {
        return index >= 0 && index < colorChoices.length ? colorChoices[index].name : fallback
    }

    function commit() {
    }

    function reload() {
        originalLogLevelStyles = cloneLogLevelStyles()
    }

    function revert() {
        for (let index = 0; index < originalLogLevelStyles.length; ++index) {
            const style = originalLogLevelStyles[index]
            UiSettings.setLogLevelForegroundColor(style.level, style.foregroundColor)
            UiSettings.setLogLevelBackgroundColor(style.level, style.backgroundColor)
        }
    }

    ColumnLayout {
        width: Math.max(0, root.availableWidth)
        spacing: 12

        Frame {
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                Label {
                    text: qsTr("Log Level Colors")
                    font.bold: true
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Choose the foreground and background colors used for each parsed log level. Changes apply immediately to open log views.")
                    color: palette.mid
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: root.rowSpacing

                    Label {
                        Layout.preferredWidth: root.levelColumnWidth
                        Layout.alignment: Qt.AlignVCenter
                        text: qsTr("Level")
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: qsTr("Foreground")
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: qsTr("Background")
                        font.bold: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Repeater {
                        model: UiSettings.logLevelStyles

                        delegate: RowLayout {
                            required property var modelData
                            readonly property int levelValue: modelData.level
                            readonly property string levelName: modelData.name
                            readonly property string foregroundColorValue: modelData.foregroundColor
                            readonly property string backgroundColorValue: modelData.backgroundColor

                            Layout.fillWidth: true
                            spacing: root.rowSpacing

                            Label {
                                Layout.preferredWidth: root.levelColumnWidth
                                Layout.alignment: Qt.AlignVCenter
                                text: parent.levelName
                            }

                            ComboBox {
                                id: foregroundCombo
                                readonly property string currentColorValue: parent.foregroundColorValue
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.alignment: Qt.AlignVCenter
                                Layout.minimumWidth: 0
                                leftPadding: 30
                                currentIndex: root.colorChoiceIndex(currentColorValue)
                                model: root.colorChoices
                                textRole: "name"

                                onActivated: function(index) {
                                    if (index >= 0) {
                                        UiSettings.setLogLevelForegroundColor(parent.levelValue, root.colorChoices[index].value)
                                    }
                                }

                                delegate: ItemDelegate {
                                    required property var modelData
                                    width: parent.width
                                    contentItem: Item {
                                        implicitHeight: 20

                                        Rectangle {
                                            id: foregroundDelegateSwatch
                                            width: 16
                                            height: 16
                                            radius: 3
                                            anchors.left: parent.left
                                            anchors.leftMargin: 2
                                            anchors.verticalCenter: parent.verticalCenter
                                            color: modelData.value
                                            border.width: 1
                                            border.color: "#7f7f7f"
                                        }

                                        Text {
                                            anchors.left: foregroundDelegateSwatch.right
                                            anchors.leftMargin: 8
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.name
                                            elide: Text.ElideRight
                                            color: foregroundCombo.palette.buttonText
                                        }
                                    }
                                }

                                Rectangle {
                                    width: 16
                                    height: 16
                                    radius: 3
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: foregroundCombo.currentColorValue
                                    border.width: 1
                                    border.color: "#7f7f7f"
                                    z: 1
                                }
                            }

                            ComboBox {
                                id: backgroundCombo
                                readonly property string currentColorValue: parent.backgroundColorValue
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.alignment: Qt.AlignVCenter
                                Layout.minimumWidth: 0
                                leftPadding: 30
                                currentIndex: root.colorChoiceIndex(currentColorValue)
                                model: root.colorChoices
                                textRole: "name"

                                onActivated: function(index) {
                                    if (index >= 0) {
                                        UiSettings.setLogLevelBackgroundColor(parent.levelValue, root.colorChoices[index].value)
                                    }
                                }

                                delegate: ItemDelegate {
                                    required property var modelData
                                    width: parent.width
                                    contentItem: Item {
                                        implicitHeight: 20

                                        Rectangle {
                                            id: backgroundDelegateSwatch
                                            width: 16
                                            height: 16
                                            radius: 3
                                            anchors.left: parent.left
                                            anchors.leftMargin: 2
                                            anchors.verticalCenter: parent.verticalCenter
                                            color: modelData.value
                                            border.width: 1
                                            border.color: "#7f7f7f"
                                        }

                                        Text {
                                            anchors.left: backgroundDelegateSwatch.right
                                            anchors.leftMargin: 8
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.name
                                            elide: Text.ElideRight
                                            color: backgroundCombo.palette.buttonText
                                        }
                                    }
                                }

                                Rectangle {
                                    width: 16
                                    height: 16
                                    radius: 3
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: backgroundCombo.currentColorValue
                                    border.width: 1
                                    border.color: "#7f7f7f"
                                    z: 1
                                }
                            }
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
