import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"

Item {
    id: root
    focus: true

    property var rowModel: null
    property bool wrapLogLines: false
    property bool followMode: false
    property string emptyText: qsTr("No rows to display.")
    property int topVisibleIndex: 0
    property var selectedIndexes: []
    property var selectedRowsByIndex: ({})
    property int selectionAnchorIndex: -1
    property int dragStartIndex: -1
    property var dragBaseSelection: []
    property int contextMenuLineIndex: -1
    property string contextMenuLineText: ""
    property real maxRowWidth: 0
    property int pendingMarkColor: 1
    readonly property bool hasSelection: selectedIndexes.length > 0
    readonly property string selectedText: selectedIndexes
        .map(function(index) {
            const row = selectedRowsByIndex[index]
            return row ? row.text : ""
        })
        .join("\n")
    readonly property bool pointerSelectionActive: dragStartIndex >= 0
    readonly property int lineCount: listView.count
    readonly property int currentLine: listView.count > 0 && rowModel ? rowModel.lineNoAt(topVisibleIndex) : 0
    readonly property alias listView: listView

    signal rowClicked(int proxyRow, int sourceRow)
    signal activated()

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

    function currentMarkColor() {
        return pendingMarkColor
    }

    function resolveLineMarkColor(markColor) {
        UiSettings.lineMarkColorsRevision
        return UiSettings.lineMarkColor(markColor)
    }

    function gutterBackgroundColor(logLevel) {
        return Qt.darker(levelBackgroundColor(logLevel), 1.12)
    }

    function updatePendingMarkColorForKey(key, active) {
        let nextColor = 1
        if (key === Qt.Key_1) {
            nextColor = 2
        } else if (key === Qt.Key_2) {
            nextColor = 3
        } else if (key === Qt.Key_3) {
            nextColor = 4
        } else if (key === Qt.Key_4) {
            nextColor = 5
        } else if (key === Qt.Key_5) {
            nextColor = 6
        } else if (key === Qt.Key_6) {
            nextColor = 7
        } else {
            return false
        }

        pendingMarkColor = active ? nextColor : 1
        return true
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
        if (!rowModel || rowIndex < 0 || rowIndex >= listView.count) {
            return
        }

        AppEngine.copyTextToClipboard(rowModel.plainTextAt(rowIndex))
    }

    function openContextMenu(rowIndex, rowTextValue, globalPoint) {
        contextMenuLineIndex = rowIndex
        contextMenuLineText = rowTextValue
        lineContextMenu.popup(globalPoint.x, globalPoint.y)
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

    function jumpByPercent(deltaPercent) {
        const maximumContentY = Math.max(0, listView.contentHeight - listView.height)
        if (maximumContentY <= 0) {
            return
        }

        const nextContentY = Math.max(0, Math.min(maximumContentY, listView.contentY + maximumContentY * deltaPercent / 100.0))
        listView.contentY = nextContentY
    }

    function selectSingleRow(index, positionMode) {
        if (!rowModel || index < 0 || index >= listView.count) {
            return false
        }

        const plainText = rowModel.plainTextAt(index)
        selectionAnchorIndex = index
        setSelectedRows([selectionEntry(index, plainText, "")])
        if (positionMode !== undefined) {
            listView.positionViewAtIndex(index, positionMode)
            updateTopVisibleIndex()
        }
        return true
    }

    function revealSourceRow(sourceRow, shouldScroll, positionMode) {
        if (!rowModel || listView.count <= 0 || sourceRow < 0) {
            return false
        }

        for (let proxyRow = 0; proxyRow < listView.count; ++proxyRow) {
            if (rowModel.sourceRowAt(proxyRow) !== sourceRow) {
                continue
            }

            selectSingleRow(proxyRow, shouldScroll ? (positionMode !== undefined ? positionMode : ListView.Visible) : undefined)
            return true
        }

        return false
    }

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: function(event) {
        if (root.updatePendingMarkColorForKey(event.key, true)) {
            event.accepted = true
        }
    }
    Keys.onReleased: function(event) {
        if (root.updatePendingMarkColorForKey(event.key, false)) {
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

    Frame {
        anchors.fill: parent

        ListView {
            id: listView
            anchors.fill: parent
            clip: true
            model: root.rowModel
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
            }
            onHeightChanged: root.updateTopVisibleIndex()
            onModelChanged: {
                root.clearSelection()
                root.maxRowWidth = 0
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
                id: rowDelegate
                required property int index
                required property int sourceRow
                required property string message
                required property string rawMessage
                required property int lineNo
                required property int logLevel
                required property bool marked
                required property int markColor
                readonly property int rowFontPixelSize: UiSettings.effectiveLogFontPixelSize

                width: ListView.view
                    ? (root.wrapLogLines
                        ? ListView.view.width - ListView.view.rightMargin
                        : Math.max(ListView.view.width - ListView.view.rightMargin, contentRow.implicitWidth + gutter.width + 18))
                    : contentRow.implicitWidth + gutter.width + 18
                height: Math.max(rowFontPixelSize, contentRow.implicitHeight) + 12
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

                Rectangle {
                    id: gutter
                    x: 0
                    y: 0
                    width: rowFontPixelSize
                    height: parent.height
                    color: root.gutterBackgroundColor(logLevel)

                    LineMarkButton {
                        width: parent.width
                        height: rowFontPixelSize
                        anchors.verticalCenter: parent.verticalCenter
                        marked: rowDelegate.marked
                        markColor: root.resolveLineMarkColor(rowDelegate.markColor)
                        onClicked: function() {
                            root.forceActiveFocus()
                            root.activated()
                            if (root.rowModel) {
                                root.rowModel.toggleMarkAt(index, root.currentMarkColor())
                            }
                        }
                    }
                }

                Item {
                    id: contentArea
                    anchors.fill: parent
                    anchors.leftMargin: gutter.width

                    Row {
                        id: contentRow
                        x: 6
                        y: 6
                        spacing: 12

                        Label {
                            id: lineNumberLabel
                            text: lineNo > 0 ? lineNo : index + 1
                            color: Qt.darker(root.levelForegroundColor(logLevel), 1.1)
                            font.family: UiSettings.logFontFamily
                            font.pixelSize: rowFontPixelSize
                        }

                        Label {
                            id: messageLabel
                            text: message.length > 0 ? message : rawMessage
                            width: root.wrapLogLines
                                ? Math.max(0, contentArea.width - lineNumberLabel.implicitWidth - contentRow.spacing - 12)
                                : implicitWidth
                            wrapMode: root.wrapLogLines ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
                            font.family: UiSettings.logFontFamily
                            font.pixelSize: rowFontPixelSize
                            color: root.levelForegroundColor(logLevel)
                        }
                    }
                }

                MouseArea {
                    anchors.fill: contentArea
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true
                    preventStealing: true

                    function rowProvider(rowIndex) {
                        const plainText = root.rowModel ? root.rowModel.plainTextAt(rowIndex) : ""
                        return root.selectionEntry(rowIndex, plainText, "")
                    }

                    onPressed: function(mouse) {
                        root.forceActiveFocus()
                        root.activated()
                        if (mouse.button === Qt.RightButton) {
                            if (!root.isIndexSelected(index)) {
                                root.setSelectedRows([root.selectionEntry(index, message, rawMessage)])
                            }
                            root.openContextMenu(index, root.rowText(message, rawMessage), mapToGlobal(mouse.x, mouse.y))
                            return
                        }

                        root.handlePressed(index, message, rawMessage, mouse.modifiers)
                    }

                    onClicked: function(mouse) {
                        if (mouse.button === Qt.LeftButton) {
                            root.rowClicked(index, sourceRow)
                        }
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

    Label {
        anchors.centerIn: parent
        visible: !!root.rowModel && root.listView.count === 0
        text: root.emptyText
        color: "#6c655c"
    }

    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: root.hasSelection
        onActivated: root.copySelectionToClipboard()
    }
}
