import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

Item {
    id: root
    focus: true

    property url sourceUrl
    property var workspace: null
    property int nodeId: -1
    property bool primaryView: true
    property var logModel: null
    property url claimedSourceUrl: ""
    readonly property int currentLine: lineList.currentLine
    readonly property int lineCount: lineList.lineCount
    readonly property string formatText: logModel ? logModel.scannerName : "Auto"
    readonly property string requestedFormatName: logModel ? logModel.requestedScannerName : "Auto"
    readonly property real linesPerSecond: logModel ? logModel.linesPerSecond : 0
    readonly property real fileSize: logModel ? logModel.fileSize : 0
    readonly property bool hasSelection: lineList.hasSelection
    readonly property string selectedText: lineList.selectedText
    readonly property bool followScrollTimerEnabled: visible && !!logModel && following && lineList.lineCount > 0
    readonly property int followScrollIntervalMs: linesPerSecond > 500
        ? UiSettings.followHighRateScrollIntervalMs
        : UiSettings.followScrollIntervalMs
    readonly property string displaySourceText: {
        if (!sourceUrl) {
            return ""
        }

        const urlText = sourceUrl.toString()
        return urlText.startsWith("file://") ? decodeURIComponent(urlText.slice(7)) : urlText
    }
    readonly property bool following: !!logModel && logModel.following
    property bool wrapLogLines: false

    function releaseLogModel() {
        if (!claimedSourceUrl || claimedSourceUrl.toString().length === 0) {
            return
        }

        AppEngine.releaseLogModel(claimedSourceUrl)
        claimedSourceUrl = ""
        logModel = null
    }

    function acquireLogModel() {
        const nextSourceUrl = sourceUrl ? sourceUrl.toString() : ""
        if (nextSourceUrl.length === 0) {
            releaseLogModel()
            return
        }

        if (claimedSourceUrl && claimedSourceUrl.toString() === nextSourceUrl && logModel) {
            return
        }

        releaseLogModel()

        const createdModel = AppEngine.createLogModel(sourceUrl)
        if (!createdModel) {
            console.error("Failed to create LogModel for source:", nextSourceUrl)
            return
        }

        claimedSourceUrl = sourceUrl
        logModel = createdModel
        wrapLogLines = AppEngine.wrapLogLinesForSource(sourceUrl)
        lineList.clearSelection()
        lineList.finishPointerSelection()
        if (createdModel.following) {
            ensureFollowingAtEnd()
        }
    }

    function ensureFollowingAtEnd() {
        if (!root.following || lineList.lineCount <= 0) {
            return
        }

        Qt.callLater(function() {
            if (!root.following || lineList.lineCount <= 0) {
                return
            }

            lineList.scrollToLast()

            Qt.callLater(function() {
                if (!root.following || lineList.lineCount <= 0) {
                    return
                }

                lineList.scrollToLast()
            })
        })
    }

    function setFollowing(enabled) {
        if (!logModel) {
            return
        }

        logModel.setFollowing(enabled)
        if (enabled) {
            ensureFollowingAtEnd()
        }
    }

    function toggleFollowing() {
        if (!logModel) {
            return
        }

        logModel.toggleFollowing()
        if (logModel.following) {
            ensureFollowingAtEnd()
        }
    }

    function scrollToFirst() {
        performManualNavigation(function() {
            lineList.scrollToFirst()
        })
    }

    function scrollToLast() {
        performManualNavigation(function() {
            lineList.scrollToLast()
        })
    }

    function scrollUpTenPercent() {
        performManualNavigation(function() {
            lineList.jumpByPercent(-10)
        })
    }

    function scrollDownTenPercent() {
        performManualNavigation(function() {
            lineList.jumpByPercent(10)
        })
    }

    function setWrapLogLines(enabled) {
        wrapLogLines = enabled
        if (primaryView && sourceUrl && sourceUrl.toString().length > 0) {
            AppEngine.saveWrapLogLinesForSource(sourceUrl, enabled)
        }
    }

    function toggleWrapLogLines() {
        setWrapLogLines(!wrapLogLines)
    }

    function setRequestedFormatName(name) {
        if (!logModel) {
            return
        }

        logModel.setRequestedScannerName(name)
    }

    function selectSingleRow(index) {
        return lineList.selectSingleRow(index, ListView.Visible)
    }

    function revealSourceRow(sourceRow, shouldScroll) {
        return lineList.revealSourceRow(sourceRow, shouldScroll, ListView.Visible)
    }

    function revealLinkedSourceRow(sourceRow) {
        if (!logModel || sourceRow < 0 || sourceRow >= lineList.lineCount) {
            return false
        }

        let revealed = false
        performManualNavigation(function() {
            revealed = lineList.selectSingleRow(sourceRow, ListView.Visible)
        })
        return revealed
    }

    function goToLine(lineNumber) {
        if (!logModel || lineList.lineCount <= 0) {
            return false
        }

        const parsedLine = Number(lineNumber)
        if (!Number.isFinite(parsedLine)) {
            return false
        }

        const targetIndex = Math.max(0, Math.min(lineList.lineCount - 1, Math.trunc(parsedLine) - 1))
        let navigated = false
        performManualNavigation(function() {
            navigated = lineList.selectSingleRow(targetIndex, ListView.Beginning)
        })
        return navigated
    }

    function performManualNavigation(navigate) {
        const wasFollowing = root.following
        navigate()
        if (!wasFollowing) {
            return
        }

        if (lineList.isEffectivelyAtEnd()) {
            ensureFollowingAtEnd()
            return
        }

        setFollowing(false)
    }

    function navigateToLevel(level, forward) {
        if (!logModel || lineList.lineCount <= 0) {
            return
        }

        const selectedIndexes = lineList.selectedIndexes
        const anchor = selectedIndexes.length > 0
            ? (forward ? selectedIndexes[selectedIndexes.length - 1] : selectedIndexes[0])
            : lineList.topVisibleIndex
        const start = Math.max(0, Math.min(lineList.lineCount - 1, anchor))
        const targetIndex = forward
            ? logModel.nextLineOfLevel(start, level)
            : logModel.previousLineOfLevel(start, level)
        if (targetIndex >= 0) {
            performManualNavigation(function() {
                lineList.selectSingleRow(targetIndex, ListView.Center)
            })
        }
    }

    function goToPreviousError() {
        navigateToLevel(0, false)
    }

    function goToNextError() {
        navigateToLevel(0, true)
    }

    function goToPreviousWarning() {
        navigateToLevel(1, false)
    }

    function goToNextWarning() {
        navigateToLevel(1, true)
    }

    function activateView() {
        forceActiveFocus()
        if (workspace) {
            workspace.setActiveView(root, nodeId)
        }
    }

    function registerWithWorkspace() {
        if (!workspace) {
            return
        }

        workspace.registerLeafView(root, nodeId, "log")
        workspace.setActiveView(root, nodeId)
    }

    function openViewMenu() {
        activateView()
        const point = viewMenuButton.mapToItem(root, 0, viewMenuButton.height)
        viewMenu.popup(point.x, point.y)
    }

    function scrollByRows(deltaRows) {
        performManualNavigation(function() {
            lineList.scrollByRows(deltaRows)
        })
    }

    function scrollByPages(deltaPages) {
        performManualNavigation(function() {
            lineList.scrollByPages(deltaPages)
        })
    }

    function scrollHorizontally(deltaPixels) {
        lineList.scrollHorizontally(deltaPixels)
    }

    function scrollToLineStart() {
        lineList.scrollToLineStart()
    }

    function scrollToSelectedLineEnd() {
        lineList.scrollToSelectedLineEnd()
    }

    onSourceUrlChanged: acquireLogModel()
    onWorkspaceChanged: registerWithWorkspace()
    onFollowingChanged: ensureFollowingAtEnd()
    onVisibleChanged: {
        if (visible) {
            ensureFollowingAtEnd()
        }
    }
    Component.onCompleted: {
        acquireLogModel()
        registerWithWorkspace()
    }
    Component.onDestruction: {
        if (workspace) {
            workspace.unregisterLeafView(root, nodeId)
        }
        releaseLogModel()
    }

    Rectangle {
        anchors.fill: parent
        color: "#d3d7dd"
    }

    Timer {
        interval: Math.max(1, root.followScrollIntervalMs)
        repeat: true
        running: root.followScrollTimerEnabled
        onTriggered: root.ensureFollowingAtEnd()
    }

    LogLineList {
        id: lineList
        anchors.fill: parent
        anchors.margins: 4
        rowModel: root.logModel
        wrapLogLines: root.wrapLogLines
        followMode: root.following
        emptyText: qsTr("Log is open, but no rows are loaded yet.")
        onActivated: root.activateView()
        onZoomWheelRequested: UiSettings.stepLogZoom(steps)
        onBackwardWheelScrollRequested: {
            if (root.following) {
                root.setFollowing(false)
            }
        }
        onUpwardScrollBarNavigationRequested: {
            if (root.following) {
                root.setFollowing(false)
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
        sequences: ["Left"]
        enabled: root.activeFocus
        onActivated: root.scrollHorizontally(-80)
    }

    Shortcut {
        sequences: ["Right"]
        enabled: root.activeFocus
        onActivated: root.scrollHorizontally(80)
    }

    Shortcut {
        sequences: ["Ctrl+Left"]
        enabled: root.activeFocus
        onActivated: root.scrollToLineStart()
    }

    Shortcut {
        sequences: ["Ctrl+Right"]
        enabled: root.activeFocus
        onActivated: root.scrollToSelectedLineEnd()
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
    }
}
