import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root

    anchors.centerIn: parent
    modal: true
    focus: true
    title: qsTr("About lgx")
    standardButtons: Dialog.Ok

    width: Math.min(parent ? parent.width - 80 : 520, 520)
    height: Math.min(parent ? parent.height - 80 : implicitHeight, 520)
    padding: 16

    contentItem: ScrollView {
        id: scrollView
        clip: true

        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        contentWidth: availableWidth

        Column {
            id: contentColumn
            width: scrollView.availableWidth
            spacing: 16

            Label {
                text: AppInfo.description
                wrapMode: Text.WordWrap
                width: parent.width
            }

            GridLayout {
                width: parent.width
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

            Label {
                text: qsTr('Developed by <a href="https://lastviking.eu/">The Last Viking LTD</a>')
                textFormat: Text.RichText
                wrapMode: Text.WordWrap
                width: parent.width
                onLinkActivated: function(link) { Qt.openUrlExternally(link) }
            }

            Label {
                text: qsTr('On GitHub: <a href="https://github.com/jgaa/lgx">https://github.com/jgaa/lgx</a>')
                textFormat: Text.RichText
                wrapMode: Text.WordWrap
                width: parent.width
                onLinkActivated: function(link) { Qt.openUrlExternally(link) }
            }
        }
    }
}