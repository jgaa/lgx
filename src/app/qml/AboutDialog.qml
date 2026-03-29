import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    anchors.centerIn: parent
    modal: true
    focus: true
    title: qsTr("About lgx")
    standardButtons: Dialog.Ok
    width: Math.min(parent ? parent.width - 80 : 520, 520)

    contentItem: ColumnLayout {
        spacing: 16

        Label {
            text: AppInfo.description
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        GridLayout {
            columns: 2
            columnSpacing: 16
            rowSpacing: 8

            Label { text: qsTr("App version") }
            Label { text: AppInfo.applicationVersion }

            Label { text: qsTr("Qt version") }
            Label { text: AppInfo.qtVersion }

            Label { text: qsTr("Boost version") }
            Label { text: AppInfo.boostVersion }

            Label { text: qsTr("Compiler") }
            Label { text: AppInfo.compiler }

            Label { text: qsTr("Build date") }
            Label { text: AppInfo.buildDate }
        }
    }
}
