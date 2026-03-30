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
    readonly property string displaySourceText: {
        if (!sourceUrl) {
            return ""
        }

        return sourceUrl.toString().startsWith("file://") ? sourceUrl.toLocalFile() : sourceUrl.toString()
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
        lineList.scrollToFirst()
    }

    function scrollToLast() {
        lineList.scrollToLast()
    }

    function scrollUpTenPercent() {
        lineList.jumpByPercent(-10)
    }

    function scrollDownTenPercent() {
        lineList.jumpByPercent(10)
    }

    function setWrapLogLines(enabled) {
        wrapLogLines = enabled
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

    function goToLine(lineNumber) {
        if (!logModel || lineList.lineCount <= 0) {
            return false
        }

        const parsedLine = Number(lineNumber)
        if (!Number.isFinite(parsedLine)) {
            return false
        }

        const targetIndex = Math.max(0, Math.min(lineList.lineCount - 1, Math.trunc(parsedLine) - 1))
        if (!!logModel && logModel.following) {
            setFollowing(false)
        }

        return lineList.selectSingleRow(targetIndex, ListView.Beginning)
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
            lineList.selectSingleRow(targetIndex, ListView.Visible)
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

        workspace.registerPrimaryLogView(root)
        workspace.setActiveView(root, nodeId)
    }

    function openViewMenu() {
        activateView()
        const point = viewMenuButton.mapToItem(null, 0, viewMenuButton.height)
        viewMenu.popup(point.x, point.y)
    }

    onSourceUrlChanged: acquireLogModel()
    onWorkspaceChanged: registerWithWorkspace()
    onFollowingChanged: ensureFollowingAtEnd()
    Component.onCompleted: {
        acquireLogModel()
        registerWithWorkspace()
    }
    Component.onDestruction: {
        if (workspace && workspace.primaryLogView === root) {
            workspace.primaryLogView = null
        }
        releaseLogModel()
    }

    Rectangle {
        anchors.fill: parent
        color: "#fbf8f2"
    }

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedModifiers: Qt.ControlModifier

        onWheel: function(event) {
            if (event.angleDelta.y === 0) {
                return
            }

            UiSettings.stepLogZoom(event.angleDelta.y > 0 ? 1 : -1)
            event.accepted = true
        }
    }

    LogLineList {
        id: lineList
        anchors.fill: parent
        anchors.margins: 20
        rowModel: root.logModel
        wrapLogLines: root.wrapLogLines
        followMode: root.following
        emptyText: qsTr("Log is open, but no rows are loaded yet.")
        onActivated: root.activateView()
    }

    SymbolToolButton {
        id: viewMenuButton
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 10
        z: 2
        visible: viewMenuButton.hovered || viewMenuButton.down || viewHoverArea.containsMouse
        symbol: "menu"
        symbolPixelSize: 18
        implicitWidth: 28
        implicitHeight: 28
        bgColor: "#efe7db"
        onClicked: root.openViewMenu()
    }

    MouseArea {
        id: viewHoverArea
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        z: 1
    }

    Menu {
        id: viewMenu

        MenuItem {
            text: qsTr("New Filter View Horizontal Split")
            onTriggered: {
                if (root.workspace) {
                    root.workspace.openNewFilterView(Qt.Vertical)
                }
            }
        }

        MenuItem {
            text: qsTr("New Filter View Vertical Split")
            onTriggered: {
                if (root.workspace) {
                    root.workspace.openNewFilterView(Qt.Horizontal)
                }
            }
        }
    }
}
