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
        { name: qsTr("White"), value: "#ffffff" },
        { name: qsTr("Ivory"), value: "#fffaf0" },
        { name: qsTr("Beige"), value: "#f5f5dc" },
        { name: qsTr("Light Yellow"), value: "#fff3a6" },
        { name: qsTr("Yellow"), value: "#facc15" },
        { name: qsTr("Amber"), value: "#f59e0b" },
        { name: qsTr("Orange"), value: "#d97706" },
        { name: qsTr("Coral"), value: "#f97316" },
        { name: qsTr("Red"), value: "#b22222" },
        { name: qsTr("Burgundy"), value: "#7f1d1d" },
        { name: qsTr("Brown"), value: "#8b5e3c" },
        { name: qsTr("Mint"), value: "#dcfce7" },
        { name: qsTr("Green"), value: "#15803d" },
        { name: qsTr("Teal"), value: "#0f766e" },
        { name: qsTr("Cyan"), value: "#0891b2" },
        { name: qsTr("Sky Blue"), value: "#0ea5e9" },
        { name: qsTr("Navy"), value: "#1e3a5f" },
        { name: qsTr("Indigo"), value: "#4338ca" },
        { name: qsTr("Violet"), value: "#7c3aed" },
        { name: qsTr("Pink"), value: "#db2777" },
        { name: qsTr("Silver"), value: "#d1d5db" },
        { name: qsTr("Gray"), value: "#6b7280" },
        { name: qsTr("Slate"), value: "#475569" },
        { name: qsTr("Charcoal"), value: "#2c2823" },
        { name: qsTr("Black"), value: "#111111" }
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
            if (colorChoices[index].value.toLowerCase() === normalizedValue) {
                return index
            }
        }
        return -1
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
                                readonly property string selectedColorValue: currentIndex >= 0 ? root.colorChoices[currentIndex].value : currentColorValue
                                readonly property string selectedColorName: currentIndex >= 0 ? root.colorChoices[currentIndex].name : currentColorValue
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.alignment: Qt.AlignVCenter
                                Layout.minimumWidth: 0
                                model: root.colorChoices
                                textRole: "name"
                                displayText: ""

                                Component.onCompleted: currentIndex = root.colorChoiceIndex(currentColorValue)
                                onCurrentColorValueChanged: currentIndex = root.colorChoiceIndex(currentColorValue)
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

                                contentItem: Text {
                                    leftPadding: 30
                                    rightPadding: foregroundCombo.indicator.width + foregroundCombo.spacing + 8
                                    text: foregroundCombo.selectedColorName
                                    font: foregroundCombo.font
                                    color: foregroundCombo.palette.buttonText
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    width: 16
                                    height: 16
                                    radius: 3
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: foregroundCombo.selectedColorValue
                                    border.width: 1
                                    border.color: "#7f7f7f"
                                    z: 1
                                }
                            }

                            ComboBox {
                                id: backgroundCombo
                                readonly property string currentColorValue: parent.backgroundColorValue
                                readonly property string selectedColorValue: currentIndex >= 0 ? root.colorChoices[currentIndex].value : currentColorValue
                                readonly property string selectedColorName: currentIndex >= 0 ? root.colorChoices[currentIndex].name : currentColorValue
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.alignment: Qt.AlignVCenter
                                Layout.minimumWidth: 0
                                model: root.colorChoices
                                textRole: "name"
                                displayText: ""

                                Component.onCompleted: currentIndex = root.colorChoiceIndex(currentColorValue)
                                onCurrentColorValueChanged: currentIndex = root.colorChoiceIndex(currentColorValue)
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

                                contentItem: Text {
                                    leftPadding: 30
                                    rightPadding: backgroundCombo.indicator.width + backgroundCombo.spacing + 8
                                    text: backgroundCombo.selectedColorName
                                    font: backgroundCombo.font
                                    color: backgroundCombo.palette.buttonText
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    width: 16
                                    height: 16
                                    radius: 3
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: backgroundCombo.selectedColorValue
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
