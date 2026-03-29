import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    property url sourceUrl
    property var logModel: null
    property url claimedSourceUrl: ""
    property int currentLine: listView.count > 0 ? topVisibleIndex + 1 : 0
    property int lineCount: listView.count
    readonly property string formatText: logModel ? logModel.scannerName : "Auto"
    readonly property string requestedFormatName: logModel ? logModel.requestedScannerName : "Auto"
    readonly property real linesPerSecond: logModel ? logModel.linesPerSecond : 0
    readonly property real fileSize: logModel ? logModel.fileSize : 0
    readonly property bool hasSelection: selectedIndexes.length > 0
    readonly property string selectedText: selectedIndexes
        .map(function(index) {
            const row = selectedRowsByIndex[index]
            return row ? row.text : ""
        })
        .join("\n")
    readonly property string displaySourceText: {
        if (!sourceUrl) {
            return ""
        }

        return sourceUrl.toString().startsWith("file://") ? sourceUrl.toLocalFile() : sourceUrl.toString()
    }
    property int topVisibleIndex: 0
    property var selectedIndexes: []
    property var selectedRowsByIndex: ({})
    property int selectionAnchorIndex: -1
    property int dragStartIndex: -1
    property var dragBaseSelection: []
    property int contextMenuLineIndex: -1
    property string contextMenuLineText: ""
    property real maxRowWidth: 0
    property bool wrapLogLines: false
    readonly property bool pointerSelectionActive: dragStartIndex >= 0
    readonly property bool following: !!logModel && logModel.following

    function normalizeIndex(index) {
        return Math.max(0, Math.min(index, listView.count - 1))
    }

    function rowText(message, rawMessage) {
        return message.length > 0 ? message : rawMessage
    }

    function levelForegroundColor(logLevel) {
        UiSettings.logLevelStylesRevision
        return UiSettings.logLevelForegroundColor(logLevel)
    }

    function levelBackgroundColor(logLevel) {
        UiSettings.logLevelStylesRevision
        return UiSettings.logLevelBackgroundColor(logLevel)
    }

    function lineShadeOpacity(index) {
        return index % 2 === 0 ? 0.0 : 0.025
    }

    function isIndexSelected(index) {
        return selectedRowsByIndex[index] !== undefined
    }

    function setSelectedRows(entries) {
        const map = {}
        const indexes = []
        const sortedEntries = entries
            .filter(function(entry) { return entry && entry.index >= 0 && entry.index < listView.count })
            .sort(function(left, right) { return left.index - right.index })

        for (let i = 0; i < sortedEntries.length; ++i) {
            const entry = sortedEntries[i]
            if (map[entry.index] !== undefined) {
                continue
            }

            map[entry.index] = entry
            indexes.push(entry.index)
        }

        selectedRowsByIndex = map
        selectedIndexes = indexes
    }

    function clearSelection() {
        setSelectedRows([])
    }

    function selectionEntry(index, message, rawMessage) {
        return {
            index: index,
            text: rowText(message, rawMessage)
        }
    }

    function addSingleSelection(index, message, rawMessage) {
        const entries = selectedIndexes.map(function(selectedIndex) {
            return selectedRowsByIndex[selectedIndex]
        })
        entries.push(selectionEntry(index, message, rawMessage))
        setSelectedRows(entries)
    }

    function replaceSelectionWithRange(startIndex, endIndex, baseEntries, rowProvider) {
        const first = Math.min(startIndex, endIndex)
        const last = Math.max(startIndex, endIndex)
        const entries = baseEntries ? baseEntries.slice() : []

        for (let index = first; index <= last; ++index) {
            entries.push(rowProvider(index))
        }

        setSelectedRows(entries)
    }

    function handlePressed(index, message, rawMessage, modifiers) {
        if (listView.count <= 0) {
            return
        }

        const normalizedIndex = normalizeIndex(index)
        dragStartIndex = normalizedIndex
        selectionAnchorIndex = normalizedIndex
        if ((modifiers & Qt.ShiftModifier) !== 0) {
            dragBaseSelection = selectedIndexes.map(function(selectedIndex) {
                return selectedRowsByIndex[selectedIndex]
            })
            addSingleSelection(normalizedIndex, message, rawMessage)
            return
        }

        dragBaseSelection = []
        setSelectedRows([selectionEntry(normalizedIndex, message, rawMessage)])
    }

    function handleDragSelection(index, rowProvider) {
        if (dragStartIndex < 0 || listView.count <= 0) {
            return
        }

        replaceSelectionWithRange(dragStartIndex, normalizeIndex(index), dragBaseSelection, rowProvider)
    }

    function finishPointerSelection() {
        dragStartIndex = -1
        dragBaseSelection = []
    }

    function copySelectionToClipboard() {
        if (!hasSelection) {
            return
        }

        AppEngine.copyTextToClipboard(selectedText)
    }

    function copyLineToClipboard(rowIndex) {
        if (rowIndex < 0 || rowIndex >= listView.count) {
            return
        }

        const plainText = logModel ? logModel.plainTextAt(rowIndex) : ""
        AppEngine.copyTextToClipboard(plainText)
    }

    function openContextMenu(rowIndex, rowTextValue, globalPoint) {
        contextMenuLineIndex = rowIndex
        contextMenuLineText = rowTextValue
        lineContextMenu.popup(globalPoint.x, globalPoint.y)
    }

    function releaseLogModel() {
        if (!claimedSourceUrl || claimedSourceUrl.toString().length === 0) {
            return
        }

        AppEngine.releaseLogModel(claimedSourceUrl)
        claimedSourceUrl = ""
        logModel = null
        maxRowWidth = 0
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
        maxRowWidth = 0
        clearSelection()
        finishPointerSelection()
        if (createdModel.following) {
            ensureFollowingAtEnd()
        }
    }

    function updateTopVisibleIndex() {
        if (listView.count <= 0) {
            topVisibleIndex = 0
            return
        }

        const visibleIndex = listView.indexAt(0, listView.contentY + 1)
        topVisibleIndex = visibleIndex >= 0 ? visibleIndex : 0
    }

    function scrollToFirst() {
        if (listView.count <= 0) {
            return
        }

        listView.positionViewAtBeginning()
    }

    function scrollToLast() {
        if (listView.count <= 0) {
            return
        }

        listView.positionViewAtEnd()
    }

    function ensureFollowingAtEnd() {
        if (!root.following || listView.count <= 0) {
            return
        }

        Qt.callLater(function() {
            if (!root.following || listView.count <= 0) {
                return
            }

            root.scrollToLast()

            Qt.callLater(function() {
                if (!root.following || listView.count <= 0) {
                    return
                }

                root.scrollToLast()
            })
        })
    }

    function setFollowing(enabled) {
        if (!logModel) {
            return
        }

        logModel.setFollowing(enabled)
        if (enabled) {
            root.ensureFollowingAtEnd()
        }
    }

    function toggleFollowing() {
        if (!logModel) {
            return
        }

        logModel.toggleFollowing()
        if (logModel.following) {
            root.ensureFollowingAtEnd()
        }
    }

    function jumpByPercent(deltaPercent) {
        const maximumContentY = Math.max(0, listView.contentHeight - listView.height)
        if (maximumContentY <= 0) {
            return
        }

        const nextContentY = Math.max(0, Math.min(maximumContentY, listView.contentY + maximumContentY * deltaPercent / 100.0))
        listView.contentY = nextContentY
    }

    function scrollUpTenPercent() {
        jumpByPercent(-10)
    }

    function scrollDownTenPercent() {
        jumpByPercent(10)
    }

    function setWrapLogLines(enabled) {
        wrapLogLines = enabled
    }

    function toggleWrapLogLines() {
        root.setWrapLogLines(!root.wrapLogLines)
    }

    function setRequestedFormatName(name) {
        if (!logModel) {
            return
        }

        logModel.setRequestedScannerName(name)
    }

    function selectSingleRow(index) {
        if (!logModel || index < 0 || index >= listView.count) {
            return
        }

        const plainText = logModel.plainTextAt(index)
        selectionAnchorIndex = index
        setSelectedRows([selectionEntry(index, plainText, "")])
        listView.positionViewAtIndex(index, ListView.Visible)
    }

    function navigateToLevel(level, forward) {
        if (!logModel || listView.count <= 0) {
            return
        }

        const anchor = selectedIndexes.length > 0
            ? (forward ? selectedIndexes[selectedIndexes.length - 1] : selectedIndexes[0])
            : topVisibleIndex
        const start = Math.max(0, Math.min(listView.count - 1, anchor))
        const targetIndex = forward
            ? logModel.nextLineOfLevel(start, level)
            : logModel.previousLineOfLevel(start, level)
        if (targetIndex >= 0) {
            selectSingleRow(targetIndex)
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

    onWrapLogLinesChanged: {
        maxRowWidth = 0
        if (wrapLogLines) {
            Qt.callLater(function() { root.updateTopVisibleIndex() })
        }
    }
    onSourceUrlChanged: acquireLogModel()
    onFollowingChanged: ensureFollowingAtEnd()
    Component.onCompleted: acquireLogModel()
    Component.onDestruction: releaseLogModel()

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

    Menu {
        id: lineContextMenu

        Menu {
            title: qsTr("Copy")

            Menu {
                title: qsTr("Selection Copy")
                enabled: root.hasSelection

                MenuItem {
                    text: qsTr("Copy")
                    enabled: root.hasSelection
                    onTriggered: root.copySelectionToClipboard()
                }
            }

            Menu {
                title: qsTr("Line")
                enabled: root.contextMenuLineIndex >= 0

                MenuItem {
                    text: qsTr("Copy")
                    enabled: root.contextMenuLineIndex >= 0
                    onTriggered: root.copyLineToClipboard(root.contextMenuLineIndex)
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: listView
                anchors.fill: parent
                clip: true
                model: root.logModel
                boundsBehavior: Flickable.StopAtBounds
                interactive: !root.pointerSelectionActive
                rightMargin: verticalScrollBar.width
                bottomMargin: horizontalScrollBar.height
                contentWidth: root.wrapLogLines ? width : Math.max(width, root.maxRowWidth)
                onContentYChanged: root.updateTopVisibleIndex()
                onCountChanged: {
                    root.updateTopVisibleIndex()
                    if (count === 0) {
                        root.clearSelection()
                        root.finishPointerSelection()
                        root.maxRowWidth = 0
                    }
                    root.ensureFollowingAtEnd()
                }
                onHeightChanged: {
                    root.updateTopVisibleIndex()
                    root.ensureFollowingAtEnd()
                }
                onModelChanged: {
                    root.clearSelection()
                    root.maxRowWidth = 0
                    root.ensureFollowingAtEnd()
                }
                onWidthChanged: {
                    if (root.wrapLogLines) {
                        root.maxRowWidth = 0
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    id: verticalScrollBar
                    policy: ScrollBar.AlwaysOn
                }

                ScrollBar.horizontal: ScrollBar {
                    id: horizontalScrollBar
                    policy: ScrollBar.AlwaysOn
                }

                delegate: Rectangle {
                    required property int index
                    required property string message
                    required property string rawMessage
                    required property int lineNo
                    required property int logLevel

                    width: ListView.view
                        ? (root.wrapLogLines
                            ? ListView.view.width - ListView.view.rightMargin
                            : Math.max(ListView.view.width - ListView.view.rightMargin, rowContent.implicitWidth + 12))
                        : rowContent.implicitWidth + 12
                    height: rowContent.implicitHeight + 12
                    color: root.isIndexSelected(index) ? "#d7e6f5" : root.levelBackgroundColor(logLevel)

                    Component.onCompleted: {
                        if (width > root.maxRowWidth) {
                            root.maxRowWidth = width
                        }
                    }

                    onWidthChanged: {
                        if (width > root.maxRowWidth) {
                            root.maxRowWidth = width
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: "#000000"
                        opacity: root.isIndexSelected(index) ? 0.0 : root.lineShadeOpacity(index)
                    }

                    Row {
                        id: rowContent
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 12

                        Label {
                            text: lineNo > 0 ? lineNo : index + 1
                            color: Qt.darker(root.levelForegroundColor(logLevel), 1.1)
                            font.family: UiSettings.logFontFamily
                            font.pixelSize: UiSettings.effectiveLogFontPixelSize
                        }

                        Label {
                            id: messageLabel
                            text: message.length > 0 ? message : rawMessage
                            width: root.wrapLogLines ? Math.max(0, parent.width - x) : implicitWidth
                            wrapMode: root.wrapLogLines ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
                            font.family: UiSettings.logFontFamily
                            font.pixelSize: UiSettings.effectiveLogFontPixelSize
                            color: root.levelForegroundColor(logLevel)
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        hoverEnabled: true
                        preventStealing: true

                        function rowProvider(rowIndex) {
                            const plainText = root.logModel ? root.logModel.plainTextAt(rowIndex) : ""
                            return root.selectionEntry(rowIndex, plainText, "")
                        }

                        onPressed: function(mouse) {
                            if (mouse.button === Qt.RightButton) {
                                if (!root.isIndexSelected(index)) {
                                    root.setSelectedRows([root.selectionEntry(index, message, rawMessage)])
                                }
                                root.openContextMenu(index, root.rowText(message, rawMessage), mapToGlobal(mouse.x, mouse.y))
                                return
                            }

                            root.handlePressed(index, message, rawMessage, mouse.modifiers)
                        }

                        onPositionChanged: function(mouse) {
                            if (!(mouse.buttons & Qt.LeftButton)) {
                                return
                            }

                            const pointInView = mapToItem(listView.contentItem, mouse.x, mouse.y)
                            const hoveredIndex = listView.indexAt(0, pointInView.y)
                            if (hoveredIndex >= 0) {
                                root.handleDragSelection(hoveredIndex, rowProvider)
                            }
                        }

                        onReleased: root.finishPointerSelection()
                        onCanceled: root.finishPointerSelection()
                    }
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: !!root.logModel && root.logModel.rowCount() === 0
        text: qsTr("Log is open, but no rows are loaded yet.")
        color: "#6c655c"
    }

    Shortcut {
        sequence: StandardKey.Copy
        enabled: root.hasSelection
        onActivated: root.copySelectionToClipboard()
    }
}
