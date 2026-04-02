import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

ScrollView {
    id: root
    anchors.fill: parent
    clip: true

    Settings {
        id: settings
    }

    function stringValue(key, fallback) {
        const value = settings.value(key, fallback)
        return value === undefined || value === null ? fallback : value.toString()
    }

    function intValue(key, fallback) {
        return parseInt(stringValue(key, fallback.toString()))
    }

    function commit() {
        settings.setValue("logging/path", logPath.text)
        settings.setValue("logging/level", logLevelFile.currentIndex.toString())
        settings.setValue("logging/applevel", logLevelApp.currentIndex.toString())
        settings.setValue("logging/prune", prune.checked ? "true" : "false")
        settings.sync()
    }

    function reload() {
        logLevelApp.currentIndex = intValue("logging/applevel", 4)
        logLevelFile.currentIndex = intValue("logging/level", 0)
        logPath.text = stringValue("logging/path", "")
        prune.checked = stringValue("logging/prune", "false") === "true"
    }

    GridLayout {
        id: formLayout
        width: root.availableWidth
        columns: width >= 420 ? 2 : 1
        rowSpacing: 8
        columnSpacing: 16

        Label {
            text: qsTr("Log Level\n(Application)")
            visible: logLevelApp.visible
        }

        ComboBox {
            id: logLevelApp
            visible: Qt.platform.os === "linux"
            Layout.fillWidth: true
            currentIndex: intValue("logging/applevel", 4)
            model: [
                qsTr("Disabled"),
                qsTr("Error"),
                qsTr("Warning"),
                qsTr("Notice"),
                qsTr("Info"),
                qsTr("Debug"),
                qsTr("Trace")
            ]
        }

        Label {
            text: qsTr("Log Level\n(File)")
        }

        ComboBox {
            id: logLevelFile
            Layout.fillWidth: true
            currentIndex: intValue("logging/level", 0)
            model: [
                qsTr("Disabled"),
                qsTr("Error"),
                qsTr("Warning"),
                qsTr("Notice"),
                qsTr("Info"),
                qsTr("Debug"),
                qsTr("Trace")
            ]
        }

        Label {
            text: qsTr("Logfile")
        }

        TextField {
            id: logPath
            Layout.fillWidth: true
            text: stringValue("logging/path", "")
            placeholderText: qsTr("/path/to/lgx.log")
        }

        Item {}

        CheckBox {
            id: prune
            text: qsTr("Prune log-file when starting")
            checked: stringValue("logging/prune", "false") === "true"
        }

        Label {
            Layout.columnSpan: formLayout.columns
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Logging changes apply on the next application start.")
            color: palette.mid
        }

        Item {
            Layout.fillHeight: true
        }
    }
}
