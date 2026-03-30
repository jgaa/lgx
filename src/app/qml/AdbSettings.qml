import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: root
    anchors.fill: parent
    clip: true

    property string originalAdbExecutablePath: ""
    property string pendingCandidatePath: ""

    function commit() {
        AppEngine.setAdbExecutablePath(adbPathField.text.trim())
    }

    function reload() {
        originalAdbExecutablePath = AppEngine.adbExecutablePath
        adbPathField.text = originalAdbExecutablePath
    }

    function revert() {
        AppEngine.setAdbExecutablePath(originalAdbExecutablePath)
        adbPathField.text = originalAdbExecutablePath
    }

    function scanForAdb() {
        const count = AppEngine.scanAdbExecutables()
        if (count === 1) {
            adbPathField.text = AppEngine.adbExecutablePath
            return
        }

        if (count > 1) {
            pendingCandidatePath = ""
            adbCandidatesDialog.open()
        }
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 12

        Frame {
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                Label {
                    text: qsTr("Android Debug Bridge")
                    font.bold: true
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Configure the adb executable used for Logcat sources. lgx does not scan for adb on startup.")
                    color: palette.mid
                }

                GridLayout {
                    width: parent.width
                    columns: width >= 460 ? 2 : 1
                    rowSpacing: 8
                    columnSpacing: 16

                    Label {
                        text: qsTr("Executable")
                    }

                    TextField {
                        id: adbPathField
                        Layout.fillWidth: true
                        placeholderText: qsTr("/path/to/adb")
                        selectByMouse: true
                    }

                    Label {
                        text: qsTr("Status")
                    }

                    Label {
                        Layout.fillWidth: true
                        text: AppEngine.adbAvailable
                              ? qsTr("Configured and executable")
                              : qsTr("Not configured")
                        color: AppEngine.adbAvailable ? "#2e7d32" : "#8b8479"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Button {
                        text: qsTr("Scan")
                        onClicked: root.scanForAdb()
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: AppEngine.adbScanError.length > 0
                        wrapMode: Text.WordWrap
                        text: AppEngine.adbScanError
                        color: "#b22222"
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }

    Dialog {
        id: adbCandidatesDialog
        parent: Overlay.overlay
        modal: true
        title: qsTr("Choose adb")
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        width: 720
        height: 420

        onAccepted: {
            if (pendingCandidatePath.length > 0) {
                adbPathField.text = pendingCandidatePath
            }
        }

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                text: qsTr("Multiple adb executables were found. Choose one.")
                wrapMode: Text.WordWrap
                color: "#6c655c"
            }

            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 0

                ListView {
                    anchors.fill: parent
                    clip: true
                    model: AppEngine.adbCandidates
                    spacing: 1

                    delegate: ItemDelegate {
                        id: candidateDelegate
                        required property string path
                        required property string version
                        required property string source

                        width: ListView.view.width
                        highlighted: path === root.pendingCandidatePath

                        contentItem: ColumnLayout {
                            spacing: 2

                            Label {
                                Layout.fillWidth: true
                                text: candidateDelegate.path
                                font.bold: true
                                elide: Text.ElideMiddle
                            }

                            Label {
                                Layout.fillWidth: true
                                text: candidateDelegate.version.length > 0
                                      ? qsTr("%1  |  %2").arg(candidateDelegate.version).arg(candidateDelegate.source)
                                      : candidateDelegate.source
                                color: "#6c655c"
                                elide: Text.ElideRight
                            }
                        }

                        onClicked: root.pendingCandidatePath = candidateDelegate.path
                    }
                }
            }
        }
    }
}
