import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    anchors.centerIn: parent
    modal: true
    focus: true
    title: qsTr("Preferences")
    standardButtons: Dialog.Ok | Dialog.Cancel
    width: Math.min(parent ? parent.width - 80 : 720, 720)
    height: Math.min(parent ? parent.height - 80 : 520, 520)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        TabBar {
            id: tabBar
            Layout.fillWidth: true

            TabButton {
                text: qsTr("General")
                width: implicitWidth
            }

            TabButton {
                text: qsTr("Logging")
                width: implicitWidth
            }

            TabButton {
                text: qsTr("Colors")
                width: implicitWidth
            }

            TabButton {
                text: qsTr("Adb")
                width: implicitWidth
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            Item {
                GeneralSettings {
                    id: generalSettings
                    anchors.fill: parent
                }
            }

            Item {
                LoggingSettings {
                    id: loggingSettings
                    anchors.fill: parent
                }
            }

            Item {
                ColorSettings {
                    id: colorSettings
                    anchors.fill: parent
                }
            }

            Item {
                AdbSettings {
                    id: adbSettings
                    anchors.fill: parent
                }
            }
        }
    }

    onAccepted: {
        generalSettings.commit()
        loggingSettings.commit()
        colorSettings.commit()
        adbSettings.commit()
        close()
    }

    onOpened: {
        generalSettings.reload()
        loggingSettings.reload()
        colorSettings.reload()
        adbSettings.reload()
    }
    onRejected: {
        generalSettings.revert()
        colorSettings.revert()
        adbSettings.revert()
        close()
    }
}
