import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    focus: true

    property url sourceUrl
    property var workspace: null
    property int nodeId: -1
    property var filterModel: null
    property var claimedFilterModel: null
    property var processOptions: [{ pid: 0, name: "", label: qsTr("All processes") }]
    property bool pendingScrollRestore: false
    property int pendingTopSourceRow: -1
    readonly property bool hasSelection: lineList.hasSelection
    readonly property string selectedText: lineList.selectedText
    readonly property bool supportsProcessFilter: !!filterModel && (filterModel.scannerName === "Logcat" || filterModel.scannerName === "Systemd")
    readonly property bool usesProcessNameFilter: !!filterModel && filterModel.scannerName === "Systemd"
    readonly property int selectedProcessIndex: usesProcessNameFilter
                                                ? processIndexForName(filterModel ? filterModel.selectedProcessName : "")
                                                : processIndexForPid(filterModel ? filterModel.selectedPid : 0)
    readonly property var levelDefinitions: [
        { level: 0, label: qsTr("Error") },
        { level: 1, label: qsTr("Warning") },
        { level: 2, label: qsTr("Notice") },
        { level: 3, label: qsTr("Info") },
        { level: 4, label: qsTr("Debug") },
        { level: 5, label: qsTr("Trace") }
    ]

    function acquireFilterModel() {
        releaseFilterModel()
        if (!sourceUrl || sourceUrl.toString().length === 0) {
            return
        }

        const createdModel = AppEngine.createFilterModel(sourceUrl)
        if (!createdModel) {
            console.error("Failed to create FilterModel for source:", sourceUrl.toString())
            return
        }

        claimedFilterModel = createdModel
        filterModel = createdModel
    }

    function defaultProcessOptions() {
        return [{ pid: 0, name: "", label: qsTr("All processes") }]
    }

    function releaseFilterModel() {
        if (!claimedFilterModel) {
            return
        }

        processRefreshTimer.stop()
        processCombo.popup.close()
        const modelToRelease = claimedFilterModel
        claimedFilterModel = null
        filterModel = null
        processOptions = defaultProcessOptions()
        AppEngine.releaseFilterModel(modelToRelease)
    }

    function activateView() {
        forceActiveFocus()
        if (workspace) {
            workspace.setActiveView(root, nodeId)
        }
    }

    function registerWithWorkspace() {
        if (workspace) {
            workspace.registerLeafView(root, nodeId, "filter")
            workspace.setActiveView(root, nodeId)
        }
    }

    function openViewMenu() {
        activateView()
        const point = viewMenuButton.mapToItem(root, 0, viewMenuButton.height)
        viewMenu.popup(point.x, point.y)
    }

    function scrollByRows(deltaRows) {
        lineList.scrollByRows(deltaRows)
    }

    function scrollByPages(deltaPages) {
        lineList.scrollByPages(deltaPages)
    }

    function toggleLevel(level, enabled) {
        if (!filterModel) {
            return
        }

        filterModel.setLevelEnabled(level, enabled)
    }

    function refreshNow() {
        if (filterModel) {
            filterModel.refresh()
        }
    }

    function processIndexForPid(pid) {
        for (let index = 0; index < processOptions.length; ++index) {
            if (processOptions[index].pid === pid) {
                return index
            }
        }
        return 0
    }

    function processIndexForName(name) {
        for (let index = 0; index < processOptions.length; ++index) {
            if (processOptions[index].name === name) {
                return index
            }
        }
        return 0
    }

    function refreshProcessOptions() {
        if (!filterModel || !supportsProcessFilter || !sourceUrl || sourceUrl.toString().length === 0) {
            processOptions = defaultProcessOptions()
            return
        }

        processOptions = usesProcessNameFilter
                         ? filterModel.systemdProcesses()
                         : AppEngine.logcatProcessesForSource(sourceUrl)
        if (!processOptions || processOptions.length === 0) {
            processOptions = defaultProcessOptions()
        }
    }

    function scheduleProcessOptionsRefresh() {
        if (supportsProcessFilter) {
            if (!processRefreshTimer.running) {
                processRefreshTimer.start()
            }
        }
    }

    function rememberScrollPositionBeforeRefresh() {
        if (!filterModel || lineList.lineCount <= 0) {
            pendingScrollRestore = false
            pendingTopSourceRow = -1
            return
        }

        pendingTopSourceRow = filterModel.sourceRowAt(lineList.topVisibleIndex)
        pendingScrollRestore = pendingTopSourceRow >= 0
    }

    function restoreScrollPositionAfterRefresh() {
        if (!pendingScrollRestore) {
            return
        }

        pendingScrollRestore = false

        if (lineList.lineCount <= 0) {
            pendingTopSourceRow = -1
            return
        }

        const targetSourceRow = pendingTopSourceRow
        pendingTopSourceRow = -1
        if (!filterModel || targetSourceRow < 0) {
            return
        }

        const targetProxyRow = filterModel.proxyRowAtOrAfterSourceRow(targetSourceRow)
        if (targetProxyRow >= 0) {
            lineList.positionViewAtRow(targetProxyRow, ListView.Beginning)
        }
    }

    onSourceUrlChanged: acquireFilterModel()
    onWorkspaceChanged: registerWithWorkspace()
    onSupportsProcessFilterChanged: {
        if (supportsProcessFilter) {
            refreshProcessOptions()
        } else {
            processOptions = defaultProcessOptions()
        }
    }
    onFilterModelChanged: {
        filterField.text = filterModel ? filterModel.pattern : ""
        if (supportsProcessFilter) {
            refreshProcessOptions()
        } else {
            processOptions = defaultProcessOptions()
        }
    }
    Component.onCompleted: {
        acquireFilterModel()
        registerWithWorkspace()
    }
    Component.onDestruction: {
        if (workspace) {
            workspace.unregisterLeafView(root, nodeId)
        }
        releaseFilterModel()
    }

    Connections {
        target: root.filterModel

        function onModelAboutToBeReset() {
            root.rememberScrollPositionBeforeRefresh()
        }

        function onModelReset() {
            root.scheduleProcessOptionsRefresh()
            Qt.callLater(root.restoreScrollPositionAfterRefresh)
        }

        function onRowsInserted(parent, first, last) {
            root.scheduleProcessOptionsRefresh()
        }

        function onRowsRemoved(parent, first, last) {
            root.scheduleProcessOptionsRefresh()
        }
    }

    Timer {
        id: processRefreshTimer
        interval: 40
        repeat: false
        onTriggered: root.refreshProcessOptions()
    }

    Rectangle {
        anchors.fill: parent
        color: "#f8f4ee"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Frame {
            Layout.fillWidth: true
            padding: 8

            RowLayout {
                width: parent.width
                spacing: 10

                Button {
                    id: levelsButton
                    text: qsTr("Levels")
                    onClicked: {
                        const point = mapToItem(root, 0, height + 4)
                        levelsPopup.x = point.x
                        levelsPopup.y = point.y
                        levelsPopup.open()
                    }
                }

                ComboBox {
                    id: processCombo
                    visible: root.supportsProcessFilter
                    model: root.processOptions
                    textRole: "label"
                    currentIndex: root.selectedProcessIndex
                    onActivated: {
                        if (root.filterModel && currentIndex >= 0 && currentIndex < root.processOptions.length) {
                            if (root.usesProcessNameFilter) {
                                root.filterModel.selectedProcessName = root.processOptions[currentIndex].name
                                root.filterModel.selectedPid = 0
                            } else {
                                root.filterModel.selectedPid = root.processOptions[currentIndex].pid
                                root.filterModel.selectedProcessName = ""
                            }
                        }
                    }
                }

                TextField {
                    id: filterField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Filter text")
                    selectByMouse: true
                    text: root.filterModel ? root.filterModel.pattern : ""
                    onTextEdited: {
                        if (root.filterModel) {
                            root.filterModel.pattern = text
                        }
                    }
                }

                CheckBox {
                    text: qsTr("raw")
                    checked: !!root.filterModel && root.filterModel.raw
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Search the full raw log line instead of only the parsed message")
                    onToggled: {
                        if (root.filterModel) {
                            root.filterModel.raw = checked
                        }
                    }
                }

                CheckBox {
                    text: qsTr("regex")
                    checked: !!root.filterModel && root.filterModel.regex
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Interpret the filter text as a regular expression")
                    onToggled: {
                        if (root.filterModel) {
                            root.filterModel.regex = checked
                        }
                    }
                }

                CheckBox {
                    text: qsTr("icase")
                    checked: !!root.filterModel && root.filterModel.caseInsensitive
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Match text without case sensitivity")
                    onToggled: {
                        if (root.filterModel) {
                            root.filterModel.caseInsensitive = checked
                        }
                    }
                }

                CheckBox {
                    text: qsTr("auto-refresh")
                    checked: !!root.filterModel && root.filterModel.autoRefresh
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Rebuild the filter automatically when the criteria or source rows change")
                    onToggled: {
                        if (root.filterModel) {
                            root.filterModel.autoRefresh = checked
                        }
                    }
                }

                SymbolToolButton {
                    enabled: !!root.filterModel
                    symbol: "refresh"
                    symbolPixelSize: 18
                    implicitWidth: 28
                    implicitHeight: 28
                    bgColor: root.filterModel && root.filterModel.dirty ? "#efd7c7" : "#e2ddd3"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Refresh filter")
                    onClicked: root.refreshNow()
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: !!root.filterModel && root.filterModel.regex && root.filterModel.regexError.length > 0
            text: root.filterModel ? root.filterModel.regexError : ""
            color: "#b22222"
        }

        LogLineList {
            id: lineList
            Layout.fillWidth: true
            Layout.fillHeight: true
            rowModel: root.filterModel
            wrapLogLines: false
            emptyText: qsTr("No matching rows.")
            onActivated: root.activateView()
            onRowClicked: function(proxyRow, sourceRow) {
                root.activateView()
                if (root.workspace) {
                    root.workspace.revealSourceRowInPrimaryLog(sourceRow)
                }
            }
        }
    }

    Popup {
        id: levelsPopup
        y: levelsButton.height + 4
        x: levelsButton.x
        width: 190
        padding: 8

        contentItem: ColumnLayout {
            spacing: 4

            Repeater {
                model: root.levelDefinitions

                delegate: CheckBox {
                    required property var modelData
                    text: modelData.label
                    checked: !!root.filterModel && root.filterModel.levelEnabled(modelData.level)
                    onToggled: root.toggleLevel(modelData.level, checked)
                }
            }

            Item {
                Layout.fillHeight: true
            }

            Button {
                Layout.alignment: Qt.AlignRight
                text: qsTr("Close")
                onClicked: levelsPopup.close()
            }
        }
    }

    PaneMenuButton {
        id: viewMenuButton
        hoverTarget: viewHoverArea
        onClicked: root.openViewMenu()
    }

    MouseArea {
        id: viewHoverArea
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        z: 1
    }

    Shortcut {
        sequences: ["Up"]
        enabled: root.activeFocus
        onActivated: root.scrollByRows(-1)
    }

    Shortcut {
        sequences: ["Down"]
        enabled: root.activeFocus
        onActivated: root.scrollByRows(1)
    }

    Shortcut {
        sequences: ["PgUp"]
        enabled: root.activeFocus
        onActivated: root.scrollByPages(-1)
    }

    Shortcut {
        sequences: ["PgDown"]
        enabled: root.activeFocus
        onActivated: root.scrollByPages(1)
    }

    Shortcut {
        sequences: ["Ctrl+L"]
        enabled: root.activeFocus && !!root.workspace
        onActivated: root.workspace.openGoToLineDialogInPrimaryLog()
    }

    Menu {
        id: viewMenu

        Menu {
            title: qsTr("Add Filter View")

            MenuItem {
                text: qsTr("Horizontal Split")
                onTriggered: {
                    if (root.workspace) {
                        root.workspace.openNewFilterView(Qt.Vertical)
                    }
                }
            }

            MenuItem {
                text: qsTr("Vertical Split")
                onTriggered: {
                    if (root.workspace) {
                        root.workspace.openNewFilterView(Qt.Horizontal)
                    }
                }
            }
        }

        Menu {
            title: qsTr("Add Marked View")

            MenuItem {
                text: qsTr("Horizontal Split")
                onTriggered: {
                    if (root.workspace) {
                        root.workspace.openNewMarkedView(Qt.Vertical)
                    }
                }
            }

            MenuItem {
                text: qsTr("Vertical Split")
                onTriggered: {
                    if (root.workspace) {
                        root.workspace.openNewMarkedView(Qt.Horizontal)
                    }
                }
            }
        }

        MenuItem {
            text: qsTr("Close")
            enabled: !!root.workspace && root.nodeId >= 0
            onTriggered: {
                if (root.workspace) {
                    root.workspace.closePane(root.nodeId)
                }
            }
        }
    }
}
