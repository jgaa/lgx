import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qml" as LgxQml
import "qml/DialogHelper.js" as DialogHelper

ApplicationWindow {
    id: window
    width: 1280
    height: 800
    visible: false
    title: Qt.application.name
    color: "#f4f1ea"

    readonly property var activeWorkspace: {
        if (tabBar.currentIndex < 0) {
            return null
        }

        const wrapper = workspaceRepeater.itemAt(tabBar.currentIndex)
        return wrapper ? wrapper.workspace : null
    }
    readonly property var activeCurrentLogModel: AppEngine.currentLogModel
    readonly property var activeLogView: activeWorkspace ? activeWorkspace.primaryLogView : null
    readonly property var activeView: activeWorkspace ? activeWorkspace.activeView : activeLogView
    readonly property url activeSourceUrl: AppEngine.currentOpenLogSourceUrl
    readonly property string activeSourceText: activeSourceUrl ? AppEngine.displaySourceTextForUrl(activeSourceUrl) : ""
    readonly property int activeLineCount: activeCurrentLogModel ? activeCurrentLogModel.lineCount : 0
    readonly property int activeCurrentLine: activeLogView ? activeLogView.currentLine : 0
    readonly property real activeLinesPerSecond: activeCurrentLogModel ? activeCurrentLogModel.linesPerSecond : 0
    readonly property real activeFileSize: activeCurrentLogModel ? activeCurrentLogModel.fileSize : 0
    readonly property string activeFormatText: activeCurrentLogModel ? activeCurrentLogModel.scannerName : "Auto"
    readonly property string activeRequestedFormatName: activeCurrentLogModel ? activeCurrentLogModel.requestedScannerName : "Auto"
    readonly property bool activeHasSelection: activeView ? activeView.hasSelection : false
    readonly property string activeSelectedText: activeView ? activeView.selectedText : ""
    readonly property bool activeFollowing: !!activeCurrentLogModel && activeCurrentLogModel.following
    readonly property bool activeWrapLogLines: activeLogView ? activeLogView.wrapLogLines : false
    readonly property var zoomMenuValues: [50, 75, 90, 100, 110, 125, 150, 175, 200]
    readonly property var windowSizePresets: [
        { label: "720p", width: 1280, height: 720 },
        { label: "900p", width: 1600, height: 900 },
        { label: "1024p", width: 1280, height: 1024 },
        { label: "1080p", width: 1920, height: 1080 },
        { label: "1440p", width: 2560, height: 1440 }
    ]

    function closeTabAt(index) {
        const closingCurrent = tabBar.currentIndex === index
        const closedBeforeCurrent = index < tabBar.currentIndex
        if (!AppEngine.closeOpenLogAt(index)) {
            return false
        }

        if (AppEngine.openLogCount === 0) {
            tabBar.currentIndex = -1
        } else if (closingCurrent) {
            tabBar.currentIndex = Math.min(index, AppEngine.openLogCount - 1)
        } else if (closedBeforeCurrent) {
            tabBar.currentIndex = tabBar.currentIndex - 1
        }

        return true
    }

    function closeAllTabs() {
        while (AppEngine.openLogCount > 0) {
            closeTabAt(AppEngine.openLogCount - 1)
        }
    }

    function setCurrentTabIndex(index) {
        tabBar.currentIndex = index
    }

    function toggleActiveFollow() {
        if (activeCurrentLogModel) {
            activeCurrentLogModel.toggleFollowing()
        }
    }

    function toggleActiveWrapLogLines() {
        if (activeLogView) {
            activeLogView.toggleWrapLogLines()
        }
    }

    function setActiveRequestedFormatName(name) {
        if (activeCurrentLogModel) {
            activeCurrentLogModel.setRequestedScannerName(name)
        }
    }

    function openGoToLineDialog() {
        if (!activeLogView || activeLineCount <= 0) {
            return
        }

        goToLineField.text = activeCurrentLine > 0 ? String(activeCurrentLine) : ""
        goToLineDialog.open()
        goToLineField.forceActiveFocus()
        goToLineField.selectAll()
    }

    function goToLineFromDialog() {
        const lineNumber = Number(goToLineField.text.trim())
        if (!activeLogView || !Number.isFinite(lineNumber) || lineNumber < 1) {
            return
        }

        if (activeFollowing) {
            activeLogView.setFollowing(false)
        }
        activeLogView.goToLine(lineNumber)
        goToLineDialog.close()
    }

    function applyWindowSizePreset(preset) {
        if (!preset) {
            return
        }

        if (window.visibility === Window.FullScreen) {
            window.showNormal()
        }

        window.width = preset.width
        window.height = preset.height
    }

    function formatLineRate(linesPerSecond) {
        const safeRate = Math.max(0, linesPerSecond)
        if (safeRate >= 10) {
            return qsTr("Rate %1 lps").arg(Number(safeRate).toLocaleString(Qt.locale(), "f", safeRate >= 100 ? 0 : 1))
        }

        const linesPerMinute = safeRate * 60.0
        return qsTr("Rate %1 lpm").arg(Number(linesPerMinute).toLocaleString(Qt.locale(), "f", linesPerMinute >= 100 ? 0 : 1))
    }

    function formatFileSize(bytes) {
        const safeBytes = Math.max(0, bytes)
        const units = ["B", "KB", "MB", "GB", "TB"]
        let value = safeBytes
        let unitIndex = 0

        while (value >= 1024 && unitIndex < units.length - 1) {
            value /= 1024.0
            unitIndex += 1
        }

        return qsTr("Size %1 %2").arg(Number(value).toLocaleString(Qt.locale(), "f", 2)).arg(units[unitIndex])
    }

    function openPipeStreamDialog() {
        openPipeStreamDialogBox.open()
    }

    function tablerIcon(name) {
        return Qt.resolvedUrl("qml/icons/tabler/" + name + ".svg")
    }

    property var dockerSelectionMap: ({})

    function openPipeStreamFromDialog() {
        const command = pipeCommandField.text.trim()
        if (command.length === 0 || (!pipeStdoutCheck.checked && !pipeStderrCheck.checked)) {
            return
        }

        const index = AppEngine.openPipeStream(
                    command,
                    pipeStdoutCheck.checked,
                    pipeStderrCheck.checked,
                    !pipeForgetCheck.checked)
        if (index >= 0) {
            tabBar.currentIndex = index
            openPipeStreamDialogBox.close()
        }
    }

    function openDockerContainersDialog() {
        dockerSelectionMap = ({})
        AppEngine.refreshDockerContainers()
        AppEngine.logUiTrace("openDockerContainersDialog opened")
        openDockerContainersDialogBox.open()
    }

    property var adbSelectionMap: ({})

    function openAdbDevicesDialog() {
        adbSelectionMap = ({})
        AppEngine.refreshAdbDevices()
        openAdbDevicesDialogBox.open()
    }

    function toggleAdbSelection(serial, name, checked) {
        AppEngine.logUiTrace("toggleAdbSelection serial='" + serial
                             + "' name='" + name
                             + "' checked=" + checked)
        const nextSelection = Object.assign({}, adbSelectionMap)
        if (checked) {
            nextSelection[serial] = name
        } else {
            delete nextSelection[serial]
        }
        adbSelectionMap = nextSelection
        AppEngine.logUiTrace("adbSelectionMap keys=[" + Object.keys(adbSelectionMap).join(", ") + "]")
    }

    function selectedAdbDeviceCount() {
        return Object.keys(adbSelectionMap).length
    }

    function openAdbDevicesFromDialog() {
        const serials = Object.keys(adbSelectionMap)
        AppEngine.logUiTrace("openAdbDevicesFromDialog serials=[" + serials.join(", ") + "]")
        if (serials.length === 0) {
            AppEngine.logUiTrace("openAdbDevicesFromDialog aborted: no selected devices")
            return
        }

        let lastIndex = -1
        for (let i = 0; i < serials.length; ++i) {
            const serial = serials[i]
            AppEngine.logUiTrace("opening adb device serial='" + serial
                                 + "' name='" + adbSelectionMap[serial] + "'")
            lastIndex = AppEngine.openAdbLogcatStream(serial, adbSelectionMap[serial])
            AppEngine.logUiTrace("openAdbLogcatStream result=" + lastIndex)
        }
        if (lastIndex >= 0) {
            tabBar.currentIndex = lastIndex
            openAdbDevicesDialogBox.close()
        }
    }

    function toggleDockerSelection(containerId, containerName, checked) {
        AppEngine.logUiTrace("toggleDockerSelection containerId='" + containerId
                             + "' name='" + containerName
                             + "' checked=" + checked)
        const nextSelection = Object.assign({}, dockerSelectionMap)
        if (checked) {
            nextSelection[containerId] = containerName
        } else {
            delete nextSelection[containerId]
        }
        dockerSelectionMap = nextSelection
        AppEngine.logUiTrace("dockerSelectionMap keys=[" + Object.keys(dockerSelectionMap).join(", ") + "]")
    }

    function selectedDockerContainerCount() {
        return Object.keys(dockerSelectionMap).length
    }

    function openDockerContainersFromDialog() {
        const containerIds = Object.keys(dockerSelectionMap)
        AppEngine.logUiTrace("openDockerContainersFromDialog containerIds=[" + containerIds.join(", ") + "]")
        if (containerIds.length === 0) {
            AppEngine.logUiTrace("openDockerContainersFromDialog aborted: no selected containers")
            return
        }

        let lastIndex = -1
        for (let i = 0; i < containerIds.length; ++i) {
            const containerId = containerIds[i]
            AppEngine.logUiTrace("opening docker container id='" + containerId
                                 + "' name='" + dockerSelectionMap[containerId] + "'")
            lastIndex = AppEngine.openDockerContainerStream(containerId, dockerSelectionMap[containerId])
            AppEngine.logUiTrace("openDockerContainerStream result=" + lastIndex)
        }
        if (lastIndex >= 0) {
            tabBar.currentIndex = lastIndex
            openDockerContainersDialogBox.close()
        }
    }

    menuBar: MenuBar {
        Menu {
            id: fileMenu
            title: qsTr("&File")

            MenuItem {
                text: qsTr("Open")
                onTriggered: {
                    const index = AppEngine.openLogFile(window.activeSourceUrl)
                    if (index >= 0) {
                        tabBar.currentIndex = index
                    }
                }
            }

            Menu {
                id: recentMenu
                title: qsTr("Recent")
                enabled: AppEngine.recentLogCount > 0

                Instantiator {
                    model: AppEngine.recentLogs

                    delegate: MenuItem {
                        required property int index
                        required property string title
                        required property url sourceUrl

                        text: title
                        onTriggered: {
                            recentMenu.dismiss()
                            Qt.callLater(function() {
                                const openedIndex = AppEngine.openRecentLogSourceAt(index)
                                if (openedIndex >= 0) {
                                    window.setCurrentTabIndex(openedIndex)
                                }
                            })
                        }
                    }

                    onObjectAdded: function(index, object) {
                        recentMenu.insertItem(index, object)
                    }

                    onObjectRemoved: function(index, object) {
                        recentMenu.removeItem(object)
                    }
                }
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("Close")
                enabled: tabBar.currentIndex >= 0
                onTriggered: window.closeTabAt(tabBar.currentIndex)
            }

            MenuItem {
                text: qsTr("Close all")
                enabled: AppEngine.openLogCount > 0
                onTriggered: window.closeAllTabs()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("Clean Cache")
                enabled: AppEngine.openStreamCount === 0
                onTriggered: AppEngine.cleanCache()
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("Quit")
                onTriggered: Qt.quit()
            }
        }

        Menu {
            title: qsTr("&Edit")

            MenuItem {
                text: qsTr("Copy")
                enabled: window.activeHasSelection
                onTriggered: AppEngine.copyTextToClipboard(window.activeSelectedText)
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("Preferences")
                onTriggered: preferencesDialog.open()
            }
        }

        Menu {
            title: qsTr("&Sources")

            MenuItem {
                text: qsTr("Open Pipe Stream")
                onTriggered: window.openPipeStreamDialog()
            }

            Menu {
                id: recentPipeStreamsMenu
                title: qsTr("Recent")
                enabled: AppEngine.recentPipeStreamCount > 0

                Instantiator {
                    model: AppEngine.recentPipeStreams

                    delegate: MenuItem {
                        required property int index
                        required property string title
                        required property url sourceUrl

                        text: title
                        onTriggered: {
                            recentPipeStreamsMenu.dismiss()
                            Qt.callLater(function() {
                                const openedIndex = AppEngine.openRecentPipeStreamAt(index)
                                if (openedIndex >= 0) {
                                    window.setCurrentTabIndex(openedIndex)
                                }
                            })
                        }
                    }

                    onObjectAdded: function(index, object) {
                        recentPipeStreamsMenu.insertItem(index, object)
                    }

                    onObjectRemoved: function(index, object) {
                        recentPipeStreamsMenu.removeItem(object)
                    }
                }
            }

            Menu {
                title: qsTr("Docker")
                visible: AppEngine.dockerAvailable

                MenuItem {
                    text: qsTr("Open Running Containers")
                    onTriggered: window.openDockerContainersDialog()
                }
            }

            Menu {
                title: qsTr("Logcat")
                enabled: AppEngine.adbAvailable

                MenuItem {
                    text: qsTr("Open Devices")
                    onTriggered: window.openAdbDevicesDialog()
                }
            }
        }

        Menu {
            id: viewMenu
            title: qsTr("&View")

            Menu {
                title: qsTr("Follow")
                enabled: !!window.activeCurrentLogModel

                MenuItem {
                    text: qsTr("Enabled")
                    checkable: true
                    checked: window.activeFollowing
                    onTriggered: window.toggleActiveFollow()
                }
            }

            Menu {
                title: qsTr("Log Lines")
                enabled: !!window.activeLogView

                MenuItem {
                    text: qsTr("Wrap long lines")
                    checkable: true
                    checked: window.activeWrapLogLines
                    onTriggered: window.toggleActiveWrapLogLines()
                }
            }

            Menu {
                id: zoomMenu
                title: qsTr("Zoom")

                Instantiator {
                    model: window.zoomMenuValues

                    delegate: MenuItem {
                        required property int modelData

                        text: qsTr("%1%").arg(modelData)
                        checkable: true
                        checked: UiSettings.logZoomPercent === modelData
                        onTriggered: UiSettings.setLogZoomPercent(modelData)
                    }

                    onObjectAdded: function(index, object) {
                        zoomMenu.insertItem(index, object)
                    }

                    onObjectRemoved: function(index, object) {
                        zoomMenu.removeItem(object)
                    }
                }
            }

            Menu {
                id: sizeMenu
                title: qsTr("Size")

                Instantiator {
                    model: window.windowSizePresets

                    delegate: MenuItem {
                        required property var modelData

                        text: qsTr("%1 (%2x%3)").arg(modelData.label).arg(modelData.width).arg(modelData.height)
                        onTriggered: window.applyWindowSizePreset(modelData)
                    }

                    onObjectAdded: function(index, object) {
                        sizeMenu.insertItem(index, object)
                    }

                    onObjectRemoved: function(index, object) {
                        sizeMenu.removeItem(object)
                    }
                }
            }

            Menu {
                id: formatMenu
                title: qsTr("Format")
                enabled: !!window.activeCurrentLogModel

                Instantiator {
                    model: AppEngine.logScanners

                    delegate: MenuItem {
                        required property string modelData

                        text: modelData
                        checkable: true
                        checked: window.activeRequestedFormatName === modelData
                        onTriggered: window.setActiveRequestedFormatName(modelData)
                    }

                    onObjectAdded: function(index, object) {
                        formatMenu.insertItem(index, object)
                    }

                    onObjectRemoved: function(index, object) {
                        formatMenu.removeItem(object)
                    }
                }
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("Goto Line")
                enabled: !!window.activeLogView && window.activeLineCount > 0
                onTriggered: window.openGoToLineDialog()
            }

            MenuSeparator {}

            MenuItem {
                text: window.visibility === Window.FullScreen ? qsTr("Exit Full Screen") : qsTr("Full Screen")
                onTriggered: {
                    if (window.visibility === Window.FullScreen) {
                        window.showNormal()
                    } else {
                        window.showFullScreen()
                    }
                }
            }
        }

        Menu {
            title: qsTr("&Windows")

            Menu {
                title: qsTr("Add Filter View")
                enabled: !!window.activeWorkspace

                MenuItem {
                    text: qsTr("Horizontal Split")
                    onTriggered: window.activeWorkspace.openNewFilterView(Qt.Vertical)
                }

                MenuItem {
                    text: qsTr("Vertical Split")
                    onTriggered: window.activeWorkspace.openNewFilterView(Qt.Horizontal)
                }
            }

            Menu {
                title: qsTr("Add Marked View")
                enabled: !!window.activeWorkspace

                MenuItem {
                    text: qsTr("Horizontal Split")
                    onTriggered: window.activeWorkspace.openNewMarkedView(Qt.Vertical)
                }

                MenuItem {
                    text: qsTr("Vertical Split")
                    onTriggered: window.activeWorkspace.openNewMarkedView(Qt.Horizontal)
                }
            }

            MenuSeparator {}

            MenuItem {
                text: qsTr("Minimize")
                onTriggered: window.showMinimized()
            }
        }

        Menu {
            title: qsTr("&Help")

            MenuItem {
                text: qsTr("About")
                onTriggered: DialogHelper.openDialog(Qt.resolvedUrl("qml/AboutDialog.qml"), window)
            }
        }
    }

    header: ToolBar {
        visible: AppEngine.openLogCount > 0
        padding: 4

        RowLayout {
            width: parent.width
            spacing: 6

            Label {
                text: qsTr("Follow")
                color: "#6c655c"
                font.bold: true
            }

            LgxQml.SymbolToolButton {
                enabled: !!window.activeCurrentLogModel
                checkable: true
                checked: window.activeFollowing
                symbol: "move_down"
                ToolTip.visible: hovered
                ToolTip.text: checked ? qsTr("Stop following live log") : qsTr("Follow live log")
                onClicked: window.toggleActiveFollow()
            }

            Frame {
                Layout.fillHeight: true
                padding: 0

                Rectangle {
                    implicitWidth: 1
                    implicitHeight: 26
                    color: "#cfc7bb"
                }
            }

            Label {
                text: qsTr("View")
                color: "#6c655c"
                font.bold: true
            }

            LgxQml.SymbolToolButton {
                enabled: !!window.activeLogView
                checkable: true
                checked: window.activeWrapLogLines
                symbol: "wrap_text"
                ToolTip.visible: hovered
                ToolTip.text: checked ? qsTr("Disable line wrapping") : qsTr("Wrap long lines")
                onClicked: window.toggleActiveWrapLogLines()
            }

            Frame {
                Layout.fillHeight: true
                padding: 0

                Rectangle {
                    implicitWidth: 1
                    implicitHeight: 26
                    color: "#cfc7bb"
                }
            }

            Label {
                text: qsTr("Navigate")
                color: "#6c655c"
                font.bold: true
            }

            LgxQml.SymbolToolButton {
                enabled: !!window.activeLogView && window.activeLineCount > 0
                iconSource: window.tablerIcon("player-skip-back")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("First line")
                accessibleName: ToolTip.text
                onClicked: window.activeLogView.scrollToFirst()
            }

            LgxQml.SymbolToolButton {
                enabled: !!window.activeLogView && window.activeLineCount > 0
                iconSource: window.tablerIcon("chevrons-up")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Jump up 10%")
                accessibleName: ToolTip.text
                onClicked: window.activeLogView.scrollUpTenPercent()
            }

            LgxQml.SymbolToolButton {
                enabled: !!window.activeLogView && window.activeLineCount > 0
                iconSource: window.tablerIcon("error-previous")
                fgColor: "#8f2626"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Previous error")
                accessibleName: ToolTip.text
                onClicked: window.activeLogView.goToPreviousError()
            }

            LgxQml.SymbolToolButton {
                enabled: !!window.activeLogView && window.activeLineCount > 0
                iconSource: window.tablerIcon("warning-previous")
                fgColor: "#8a5a00"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Previous warning")
                accessibleName: ToolTip.text
                onClicked: window.activeLogView.goToPreviousWarning()
            }

            Frame {
                Layout.fillHeight: true
                padding: 0

                Rectangle {
                    implicitWidth: 1
                    implicitHeight: 26
                    color: "#cfc7bb"
                }
            }

            LgxQml.SymbolToolButton {
                enabled: !!window.activeLogView && window.activeLineCount > 0
                iconSource: window.tablerIcon("warning-next")
                fgColor: "#8a5a00"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Next warning")
                accessibleName: ToolTip.text
                onClicked: window.activeLogView.goToNextWarning()
            }

            LgxQml.SymbolToolButton {
                enabled: !!window.activeLogView && window.activeLineCount > 0
                iconSource: window.tablerIcon("error-next")
                fgColor: "#8f2626"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Next error")
                accessibleName: ToolTip.text
                onClicked: window.activeLogView.goToNextError()
            }

            LgxQml.SymbolToolButton {
                enabled: !!window.activeLogView && window.activeLineCount > 0
                iconSource: window.tablerIcon("chevrons-down")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Jump down 10%")
                accessibleName: ToolTip.text
                onClicked: window.activeLogView.scrollDownTenPercent()
            }

            LgxQml.SymbolToolButton {
                enabled: !!window.activeLogView && window.activeLineCount > 0
                iconSource: window.tablerIcon("player-skip-forward")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Last line")
                accessibleName: ToolTip.text
                onClicked: window.activeLogView.scrollToLast()
            }

            Item {
                Layout.fillWidth: true
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "silver" }
            GradientStop { position: 1.0; color: "darkgrey" }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            visible: !emptyBackground.visible
            onCurrentIndexChanged: AppEngine.setCurrentOpenLogIndex(currentIndex)

            Repeater {
                model: AppEngine.openLogs

                TabButton {
                    id: tabButton
                    required property int index
                    required property string title
                    required property url sourceUrl
                    property var tabLogModel: null
                    readonly property string sourceScheme: sourceUrl ? sourceUrl.toString().split(":")[0] : ""
                    readonly property bool isStreamSource: sourceScheme === "pipe" || sourceScheme === "docker" || sourceScheme === "adb"
                    readonly property url sourceIconSource: sourceScheme === "docker"
                                                           ? window.tablerIcon("packages")
                                                           : (sourceScheme === "pipe"
                                                              ? window.tablerIcon("terminal-2")
                                                              : (sourceScheme === "adb"
                                                                 ? window.tablerIcon("device-mobile")
                                                                 : window.tablerIcon("file-text")))

                    padding: 0
                    implicitWidth: tabContent.implicitWidth + 24
                    
                    Component.onCompleted: {
                        if (sourceUrl && sourceUrl.toString().length > 0) {
                            tabLogModel = AppEngine.createLogModel(sourceUrl)
                        }
                    }

                    Component.onDestruction: {
                        if (sourceUrl && sourceUrl.toString().length > 0) {
                            AppEngine.releaseLogModel(sourceUrl)
                        }
                    }

                    contentItem: RowLayout {
                        id: tabContent
                        spacing: 6

                        Image {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.leftMargin: 10
                            source: tabButton.sourceIconSource
                            sourceSize.width: 16
                            sourceSize.height: 16
                            width: 16
                            height: 16
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                        }

                        Label {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.fillWidth: true
                            text: title
                            elide: Text.ElideRight
                            font.bold: !!tabButton.tabLogModel && tabButton.tabLogModel.active
                        }

                        LgxQml.SymbolToolButton {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: 18
                            implicitHeight: 18
                            symbolPixelSize: 12
                            symbol: "close"
                            fgColor: "#6c655c"
                            bgColor: tabButton.checked ? "#d7d2c9" : "#e8e2d8"
                            focusPolicy: Qt.NoFocus
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Close tab")
                            onClicked: window.closeTabAt(index)
                        }
                    }
                }
            }
        }

        Component.onCompleted: AppEngine.setCurrentOpenLogIndex(tabBar.currentIndex)

        StackLayout {
            id: workspaceStack
            visible: !emptyBackground.visible
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            Repeater {
                id: workspaceRepeater
                model: AppEngine.openLogs

                delegate: Item {
                    required property url sourceUrl
                    property alias workspace: workspaceItem
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    LgxQml.LogWorkspace {
                        id: workspaceItem
                        anchors.fill: parent
                        sourceUrl: parent.sourceUrl
                    }
                }
            }
        }

        Item {
            id: emptyBackground
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: AppEngine.openLogCount === 0 || tabBar.currentIndex < 0

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: "lgx"
                    font.pixelSize: 42
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: AppInfo ? AppInfo.description : ""
                    font.pixelSize: 18
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: AppEngine.dockerAvailable
                          ? (AppEngine.adbAvailable
                             ? qsTr("Open a log file from File/Open or a stream from Sources/Open Pipe Stream, Sources/Docker, or Sources/Logcat.")
                             : qsTr("Open a log file from File/Open or a stream from Sources/Open Pipe Stream or Sources/Docker."))
                          : (AppEngine.adbAvailable
                             ? qsTr("Open a log file from File/Open or a stream from Sources/Open Pipe Stream or Sources/Logcat.")
                             : qsTr("Open a log file from File/Open or a stream from Sources/Open Pipe Stream."))
                    color: "#6c655c"
                }
            }
        }
    }

    footer: Frame {
        visible: AppEngine.openLogCount > 0
        topPadding: 3
        bottomPadding: 3
        leftPadding: 6
        rightPadding: 6

        RowLayout {
            width: parent.width
            spacing: 6

            Label {
                text: qsTr("Source")
                color: "#6c655c"
            }

            TextInput {
                id: sourceField
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                readOnly: true
                selectByMouse: true
                text: window.activeSourceText
                color: "#2c2823"
                selectedTextColor: "#fbf8f2"
                selectionColor: "#7f9bb8"
                clip: true
                topPadding: 0
                bottomPadding: 0
            }

            LgxQml.SymbolToolButton {
                enabled: sourceField.text.length > 0
                symbol: "content_copy"
                fgColor: "#2c2823"
                bgColor: "#e2ddd3"
                onClicked: AppEngine.copyTextToClipboard(sourceField.text)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Copy source")
            }

            Label {
                text: qsTr("Lines %1").arg(window.activeLineCount)
                color: "#6c655c"
            }

            Label {
                text: qsTr("Line %1").arg(window.activeCurrentLine)
                color: "#6c655c"
            }

            Label {
                text: qsTr("Format %1").arg(window.activeFormatText)
                color: "#6c655c"
            }

            Label {
                text: window.formatFileSize(window.activeFileSize)
                color: "#6c655c"
            }

            Label {
                text: window.formatLineRate(window.activeLinesPerSecond)
                color: "#6c655c"
            }

            Label {
                id: zoomStatusLabel
                text: qsTr("Zoom %1%").arg(UiSettings.logZoomPercent)
                color: "#6c655c"

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onDoubleClicked: UiSettings.resetLogZoom()
                }
            }
        }
    }

    PreferencesDialog {
        id: preferencesDialog
    }

    Dialog {
        id: openPipeStreamDialogBox
        parent: Overlay.overlay
        modal: true
        title: qsTr("Open Pipe Stream")
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        width: 560

        onOpened: {
            pipeCommandField.forceActiveFocus()
            pipeCommandField.selectAll()
            pipeForgetCheck.checked = false
        }
        onAccepted: window.openPipeStreamFromDialog()

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                text: qsTr("Command")
                color: "#6c655c"
            }

            TextField {
                id: pipeCommandField
                Layout.fillWidth: true
                placeholderText: qsTr("journalctl -f")
                selectByMouse: true
                onAccepted: {
                    if (text.trim().length > 0 && (pipeStdoutCheck.checked || pipeStderrCheck.checked)) {
                        window.openPipeStreamFromDialog()
                    }
                }
            }

            Label {
                text: qsTr("Capture")
                color: "#6c655c"
            }

            RowLayout {
                spacing: 16

                CheckBox {
                    id: pipeStdoutCheck
                    text: qsTr("stdout")
                    checked: true
                }

                CheckBox {
                    id: pipeStderrCheck
                    text: qsTr("stderr")
                    checked: true
                }

                CheckBox {
                    id: pipeForgetCheck
                    text: qsTr("forget")
                    checked: false
                }
            }

            Label {
                visible: !pipeStdoutCheck.checked && !pipeStderrCheck.checked
                text: qsTr("Select at least one stream.")
                color: "#b22222"
            }
        }
    }

    Dialog {
        id: openDockerContainersDialogBox
        parent: Overlay.overlay
        modal: true
        title: qsTr("Open Docker Container Logs")
        standardButtons: Dialog.Open | Dialog.Cancel
        anchors.centerIn: parent
        width: 720
        height: 520

        onAccepted: {
            AppEngine.logUiTrace("openDockerContainersDialog accepted")
            window.openDockerContainersFromDialog()
        }

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                text: qsTr("Running containers")
                color: "#6c655c"
            }

            RowLayout {
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    text: AppEngine.dockerContainerQueryError.length > 0
                          ? AppEngine.dockerContainerQueryError
                          : (AppEngine.dockerContainerCount > 0
                             ? qsTr("%1 container(s)").arg(AppEngine.dockerContainerCount)
                             : qsTr("No running containers found."))
                    color: AppEngine.dockerContainerQueryError.length > 0 ? "#b22222" : "#6c655c"
                    wrapMode: Text.Wrap
                }

                Button {
                    text: qsTr("Refresh")
                    onClicked: AppEngine.refreshDockerContainers()
                }
            }

            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 0

                ListView {
                    anchors.fill: parent
                    clip: true
                    model: AppEngine.dockerContainers
                    spacing: 1

                    delegate: ItemDelegate {
                        id: dockerDelegate
                        required property string containerId
                        required property string name
                        required property string image
                        required property string status

                        readonly property bool selected: !!window.dockerSelectionMap[dockerDelegate.containerId]

                        function applySelection(checked) {
                            window.toggleDockerSelection(
                                        dockerDelegate.containerId,
                                        dockerDelegate.name,
                                        checked)
                        }

                        function toggleSelection() {
                            applySelection(!dockerDelegate.selected)
                        }

                        width: ListView.view.width
                        padding: 10

                        contentItem: RowLayout {
                            spacing: 10

                            CheckBox {
                                id: dockerCheck
                                checked: dockerDelegate.selected
                                onClicked: dockerDelegate.applySelection(checked)
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Label {
                                    Layout.fillWidth: true
                                    text: dockerDelegate.name.length > 0
                                          ? dockerDelegate.name
                                          : dockerDelegate.containerId
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("%1  |  %2").arg(dockerDelegate.image, dockerDelegate.status)
                                    color: "#6c655c"
                                    elide: Text.ElideRight
                                }
                            }

                            Label {
                                text: dockerDelegate.containerId
                                color: "#8b8479"
                            }
                        }

                        onClicked: dockerDelegate.toggleSelection()
                    }
                }
            }

            Label {
                visible: window.selectedDockerContainerCount() === 0
                text: qsTr("Select one or more containers.")
                color: "#b22222"
            }
        }
    }

    Dialog {
        id: openAdbDevicesDialogBox
        parent: Overlay.overlay
        modal: true
        title: qsTr("Open Logcat")
        standardButtons: Dialog.Open | Dialog.Cancel
        anchors.centerIn: parent
        width: 720
        height: 520

        onAccepted: {
            AppEngine.logUiTrace("openAdbDevicesDialog accepted")
            window.openAdbDevicesFromDialog()
        }
        onOpened: AppEngine.logUiTrace("openAdbDevicesDialog opened")

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                text: qsTr("Connected devices")
                color: "#6c655c"
            }

            RowLayout {
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    text: AppEngine.adbDeviceQueryError.length > 0
                          ? AppEngine.adbDeviceQueryError
                          : (AppEngine.adbDeviceCount > 0
                             ? qsTr("%1 device(s)").arg(AppEngine.adbDeviceCount)
                             : qsTr("No connected devices found."))
                    color: AppEngine.adbDeviceQueryError.length > 0 ? "#b22222" : "#6c655c"
                    wrapMode: Text.Wrap
                }

                Button {
                    text: qsTr("Refresh")
                    onClicked: AppEngine.refreshAdbDevices()
                }
            }

            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 0

                ListView {
                    anchors.fill: parent
                    clip: true
                    model: AppEngine.adbDevices
                    spacing: 1

                    delegate: ItemDelegate {
                        id: adbDelegate
                        required property string serial
                        required property string state
                        required property string model
                        required property string device
                        required property string product

                        readonly property bool selectable: state === "device"
                        readonly property bool selected: !!window.adbSelectionMap[adbDelegate.serial]

                        function applySelection(checked) {
                            window.toggleAdbSelection(
                                        adbDelegate.serial,
                                        adbDelegate.model.length > 0 ? adbDelegate.model : adbDelegate.device,
                                        checked)
                        }

                        function toggleSelection() {
                            if (!adbDelegate.selectable) {
                                return
                            }
                            applySelection(!adbDelegate.selected)
                        }

                        width: ListView.view.width
                        padding: 10
                        enabled: selectable

                        contentItem: RowLayout {
                            spacing: 10

                            CheckBox {
                                id: adbCheck
                                enabled: adbDelegate.selectable
                                checked: adbDelegate.selected
                                onClicked: adbDelegate.applySelection(checked)
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                Label {
                                    Layout.fillWidth: true
                                    text: adbDelegate.model.length > 0
                                          ? adbDelegate.model
                                          : (adbDelegate.device.length > 0 ? adbDelegate.device : adbDelegate.serial)
                                    font.bold: true
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("%1  |  %2  |  %3")
                                          .arg(adbDelegate.serial)
                                          .arg(adbDelegate.product)
                                          .arg(adbDelegate.state)
                                    color: "#6c655c"
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        onClicked: {
                            adbDelegate.toggleSelection()
                        }
                    }
                }
            }

            Label {
                visible: window.selectedAdbDeviceCount() === 0
                text: qsTr("Select one or more online devices.")
                color: "#b22222"
            }
        }
    }

    Dialog {
        id: goToLineDialog
        parent: Overlay.overlay
        modal: true
        title: qsTr("Goto Line")
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        width: 320

        onOpened: {
            goToLineField.forceActiveFocus()
            goToLineField.selectAll()
        }
        onAccepted: window.goToLineFromDialog()

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                text: qsTr("Line number")
                color: "#6c655c"
            }

            TextField {
                id: goToLineField
                Layout.fillWidth: true
                placeholderText: qsTr("1 - %1").arg(window.activeLineCount)
                selectByMouse: true
                validator: IntValidator {
                    bottom: 1
                    top: Math.max(1, window.activeLineCount)
                }
                onAccepted: {
                    if (acceptableInput) {
                        window.goToLineFromDialog()
                    }
                }
            }
        }
    }

    Shortcut {
        sequence: "f"
        enabled: !!window.activeCurrentLogModel
        onActivated: window.toggleActiveFollow()
    }

    Shortcut {
        sequence: "Ctrl+L"
        enabled: !!window.activeLogView && window.activeLineCount > 0
        onActivated: window.openGoToLineDialog()
    }

    Shortcut {
        sequence: "Ctrl+F"
        enabled: !!window.activeWorkspace
        onActivated: window.activeWorkspace.openNewFilterView(Qt.Vertical)
    }

    Shortcut {
        sequence: "Ctrl+Shift+F"
        enabled: !!window.activeWorkspace
        onActivated: window.activeWorkspace.openNewFilterView(Qt.Horizontal)
    }

    Shortcut {
        sequence: "Ctrl+M"
        enabled: !!window.activeWorkspace
        onActivated: window.activeWorkspace.openNewMarkedView(Qt.Vertical)
    }

    Shortcut {
        sequence: "Ctrl+Shift+M"
        enabled: !!window.activeWorkspace
        onActivated: window.activeWorkspace.openNewMarkedView(Qt.Horizontal)
    }
}
