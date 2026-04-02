import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

ScrollView {
    id: root
    anchors.fill: parent
    clip: true
    property string originalLogFontFamily: ""
    property int originalLogBaseFontPixelSize: UiSettings.logBaseFontPixelSize
    property int originalLogZoomPercent: UiSettings.logZoomPercent
    property bool originalFollowLiveLogsByDefault: UiSettings.followLiveLogsByDefault
    property bool originalWrapLogLinesByDefault: UiSettings.wrapLogLinesByDefault
    property int originalFollowScrollIntervalMs: UiSettings.followScrollIntervalMs
    property int originalFollowHighRateScrollIntervalMs: UiSettings.followHighRateScrollIntervalMs
    property string originalDefaultLogScannerName: UiSettings.defaultLogScannerName

    Settings {
        id: settings
        category: "startup"
        property bool restoreOpenLogsOnStartup: false
    }

    function syncAppearanceControls() {
        logFontFamilyCombo.editText = UiSettings.logFontFamily
        const fontFamilyIndex = logFontFamilyCombo.find(UiSettings.logFontFamily)
        logFontFamilyCombo.currentIndex = fontFamilyIndex
        logBaseFontSizeSpin.value = UiSettings.logBaseFontPixelSize
        logZoomSpin.value = UiSettings.logZoomPercent
    }

    function applyLogFontFamily() {
        const selectedFont = logFontFamilyCombo.editText.trim()
        if (selectedFont.length === 0) {
            syncAppearanceControls()
            return
        }

        UiSettings.setLogFontFamily(selectedFont)
        syncAppearanceControls()
    }

    function commit() {
        settings.restoreOpenLogsOnStartup = restoreOnStartup.checked
        settings.sync()
    }

    function reload() {
        restoreOnStartup.checked = settings.restoreOpenLogsOnStartup
        originalLogFontFamily = UiSettings.logFontFamily
        originalLogBaseFontPixelSize = UiSettings.logBaseFontPixelSize
        originalLogZoomPercent = UiSettings.logZoomPercent
        originalFollowLiveLogsByDefault = UiSettings.followLiveLogsByDefault
        originalWrapLogLinesByDefault = UiSettings.wrapLogLinesByDefault
        originalFollowScrollIntervalMs = UiSettings.followScrollIntervalMs
        originalFollowHighRateScrollIntervalMs = UiSettings.followHighRateScrollIntervalMs
        originalDefaultLogScannerName = UiSettings.defaultLogScannerName
        followLiveLogsByDefaultCheck.checked = UiSettings.followLiveLogsByDefault
        wrapLogLinesByDefaultCheck.checked = UiSettings.wrapLogLinesByDefault
        followScrollIntervalSpin.value = UiSettings.followScrollIntervalMs
        followHighRateScrollIntervalSpin.value = UiSettings.followHighRateScrollIntervalMs
        defaultScannerCombo.currentIndex = defaultScannerCombo.find(UiSettings.defaultLogScannerName)
        syncAppearanceControls()
    }

    function revert() {
        UiSettings.setLogFontFamily(originalLogFontFamily)
        UiSettings.setLogBaseFontPixelSize(originalLogBaseFontPixelSize)
        UiSettings.setLogZoomPercent(originalLogZoomPercent)
        UiSettings.setFollowLiveLogsByDefault(originalFollowLiveLogsByDefault)
        UiSettings.setWrapLogLinesByDefault(originalWrapLogLinesByDefault)
        UiSettings.setFollowScrollIntervalMs(originalFollowScrollIntervalMs)
        UiSettings.setFollowHighRateScrollIntervalMs(originalFollowHighRateScrollIntervalMs)
        UiSettings.setDefaultLogScannerName(originalDefaultLogScannerName)
        followLiveLogsByDefaultCheck.checked = UiSettings.followLiveLogsByDefault
        wrapLogLinesByDefaultCheck.checked = UiSettings.wrapLogLinesByDefault
        followScrollIntervalSpin.value = UiSettings.followScrollIntervalMs
        followHighRateScrollIntervalSpin.value = UiSettings.followHighRateScrollIntervalMs
        defaultScannerCombo.currentIndex = defaultScannerCombo.find(UiSettings.defaultLogScannerName)
        syncAppearanceControls()
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 12

        CheckBox {
            id: restoreOnStartup
            text: qsTr("Restore open logs on startup")
            checked: settings.restoreOpenLogsOnStartup
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("When enabled, lgx reopens the log sources that were open when the previous session ended.")
            color: palette.mid
        }

        Frame {
            Layout.fillWidth: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                Label {
                    text: qsTr("Log View Appearance")
                    font.bold: true
                }

                GridLayout {
                    width: parent.width
                    columns: width >= 420 ? 2 : 1
                    rowSpacing: 8
                    columnSpacing: 16

                    Label {
                        text: qsTr("Font")
                    }

                    ComboBox {
                        id: logFontFamilyCombo
                        Layout.fillWidth: true
                        model: UiSettings.logFontFamilies
                        editable: true
                        onActivated: root.applyLogFontFamily()
                        onAccepted: root.applyLogFontFamily()
                    }

                    Label {
                        text: qsTr("Font Size")
                    }

                    SpinBox {
                        id: logBaseFontSizeSpin
                        Layout.fillWidth: true
                        from: UiSettings.minLogBaseFontPixelSize
                        to: UiSettings.maxLogBaseFontPixelSize
                        editable: true
                        onValueModified: UiSettings.setLogBaseFontPixelSize(value)
                    }

                    Label {
                        text: qsTr("Zoom")
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        SpinBox {
                            id: logZoomSpin
                            Layout.fillWidth: true
                            from: UiSettings.minLogZoomPercent
                            to: UiSettings.maxLogZoomPercent
                            stepSize: 10
                            editable: true
                            onValueModified: UiSettings.setLogZoomPercent(value)
                        }

                        Label {
                            text: "%"
                            color: palette.mid
                        }

                        Button {
                            text: qsTr("Reset")
                            enabled: UiSettings.logZoomPercent !== 100
                            onClicked: UiSettings.resetLogZoom()
                        }
                    }

                    Label {
                        text: qsTr("Default Follow")
                    }

                    CheckBox {
                        id: followLiveLogsByDefaultCheck
                        Layout.fillWidth: true
                        text: qsTr("Follow live logs automatically")
                        checked: UiSettings.followLiveLogsByDefault
                        onToggled: UiSettings.setFollowLiveLogsByDefault(checked)
                    }

                    Label {
                        text: qsTr("Default Wrap")
                    }

                    CheckBox {
                        id: wrapLogLinesByDefaultCheck
                        Layout.fillWidth: true
                        text: qsTr("Wrap long log lines automatically")
                        checked: UiSettings.wrapLogLinesByDefault
                        onToggled: UiSettings.setWrapLogLinesByDefault(checked)
                    }

                    Label {
                        text: qsTr("Default Format")
                    }

                    ComboBox {
                        id: defaultScannerCombo
                        Layout.fillWidth: true
                        model: UiSettings.availableLogScannerNames
                        currentIndex: find(UiSettings.defaultLogScannerName)
                        onActivated: UiSettings.setDefaultLogScannerName(currentText)
                    }

                    Label {
                        text: qsTr("Follow Scroll")
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        SpinBox {
                            id: followScrollIntervalSpin
                            Layout.fillWidth: true
                            from: 50
                            to: 60000
                            stepSize: 50
                            editable: true
                            value: UiSettings.followScrollIntervalMs
                            onValueModified: UiSettings.setFollowScrollIntervalMs(value)
                        }

                        Label {
                            text: qsTr("ms")
                            color: palette.mid
                        }
                    }

                    Label {
                        text: qsTr("Follow Scroll Above 500 lps")
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        SpinBox {
                            id: followHighRateScrollIntervalSpin
                            Layout.fillWidth: true
                            from: 50
                            to: 60000
                            stepSize: 100
                            editable: true
                            value: UiSettings.followHighRateScrollIntervalMs
                            onValueModified: UiSettings.setFollowHighRateScrollIntervalMs(value)
                        }

                        Label {
                            text: qsTr("ms")
                            color: palette.mid
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
