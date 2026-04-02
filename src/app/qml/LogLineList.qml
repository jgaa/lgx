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
    property int popupLineIndex: -1
    property string popupLineText: ""
    property real maxRowWidth: 0
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
    signal pageScrollRequested(int deltaPages)

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

    function rowBackgroundColor(index, logLevel) {
        const baseColor = levelBackgroundColor(logLevel)
        return index % 2 === 0 ? baseColor : Qt.darker(baseColor, 1.025)
    }

    function currentMarkColor() {
        return AppEngine.activeLineMarkColor()
    }

    function isIndexSelected(index) {
        return selectedRowsByIndex[index] !== undefined
    }

    function resolveLineMarkColor(markColor) {
        UiSettings.lineMarkColorsRevision
        return UiSettings.lineMarkColor(markColor)
    }

    function gutterBackgroundColor(logLevel) {
        return Qt.darker(levelBackgroundColor(logLevel), 1.12)
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

    function openContextMenu(rowIndex, rowTextValue, menuPoint) {
        contextMenuLineIndex = rowIndex
        contextMenuLineText = rowTextValue
        lineContextMenu.popup(menuPoint.x, menuPoint.y)
    }

    function popupCopyText() {
        if (!linePopupTextEdit) {
            return
        }

        const text = linePopupTextEdit.selectedText.length > 0
            ? linePopupTextEdit.selectedText
            : popupLineText
        if (text.length > 0) {
            AppEngine.copyTextToClipboard(text)
        }
    }

    function openLinePopup(rowIndex, rowTextValue, menuPoint) {
        popupLineIndex = rowIndex
        popupLineText = rowTextValue
        linePopup.open()
        Qt.callLater(function() {
            const margin = 12
            const popupWidth = linePopup.width
            const popupHeight = linePopup.height
            linePopup.x = Math.max(margin, Math.min(root.width - popupWidth - margin, menuPoint.x))
            linePopup.y = Math.max(margin, Math.min(root.height - popupHeight - margin, menuPoint.y))
            linePopupTextEdit.forceActiveFocus()
            linePopupTextEdit.deselect()
        })
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

    function isEffectivelyAtEnd() {
        const maximumContentY = Math.max(0, listView.contentHeight - listView.height)
        if (maximumContentY <= 0) {
            return true
        }

        return listView.contentY >= maximumContentY - 1
    }

    function jumpByPercent(deltaPercent) {
        const maximumContentY = Math.max(0, listView.contentHeight - listView.height)
        if (maximumContentY <= 0 || deltaPercent === 0) {
            return
        }

        const nextContentY = Math.max(0, Math.min(maximumContentY, listView.contentY + maximumContentY * deltaPercent / 100.0))
        if (nextContentY <= 1) {
            scrollToFirst()
            return
        }

        if (nextContentY >= maximumContentY - 1) {
            scrollToLast()
            return
        }

        listView.contentY = nextContentY
    }

    function scrollByPages(deltaPages) {
        const maximumContentY = Math.max(0, listView.contentHeight - listView.height)
        if (maximumContentY <= 0 || deltaPages === 0) {
            return
        }

        const nextContentY = Math.max(0, Math.min(maximumContentY, listView.contentY + (listView.height * deltaPages)))
        listView.contentY = nextContentY
        updateTopVisibleIndex()
    }

    function scrollByRows(deltaRows) {
        if (listView.count <= 0 || deltaRows === 0) {
            return
        }

        const targetIndex = normalizeIndex(topVisibleIndex + deltaRows)
        listView.positionViewAtIndex(targetIndex, ListView.Beginning)
        updateTopVisibleIndex()
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

    function positionViewAtRow(index, positionMode) {
        if (index < 0 || index >= listView.count) {
            return false
        }

        listView.positionViewAtIndex(index, positionMode !== undefined ? positionMode : ListView.Beginning)
        updateTopVisibleIndex()
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
        if (event.key === Qt.Key_PageUp) {
            root.pageScrollRequested(-1)
            root.scrollByPages(-1)
            event.accepted = true
            return
        }

        if (event.key === Qt.Key_PageDown) {
            root.pageScrollRequested(1)
            root.scrollByPages(1)
            event.accepted = true
        }
    }

    Menu {
        id: lineContextMenu

        Menu {
            title: qsTr("Copy")
            MenuItem {
                text: qsTr("Selection")
                enabled: root.hasSelection
                onTriggered: root.copySelectionToClipboard()
            }

            MenuItem {
                text: qsTr("Line")
                enabled: root.contextMenuLineIndex >= 0
                onTriggered: root.copyLineToClipboard(root.contextMenuLineIndex)
            }
        }
    }

    Popup {
        id: linePopup
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 12
        width: {
            const availableWidth = Math.max(120, root.width - 24)
            return Math.min(availableWidth, Math.max(240, Math.round(root.width * 0.72)))
        }
        height: {
            const availableHeight = Math.max(120, root.height - 24)
            return Math.min(availableHeight, Math.max(180, Math.round(root.height * 0.72)))
        }

        background: Rectangle {
            radius: 8
            color: "#f8f4ee"
            border.width: 1
            border.color: "#c9c0b2"
        }

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: root.popupLineIndex >= 0
                    ? qsTr("Line %1").arg(root.rowModel ? root.rowModel.lineNoAt(root.popupLineIndex) : root.popupLineIndex + 1)
                    : qsTr("Line")
                font.bold: true
                color: "#4d453b"
            }

            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 0

                ScrollView {
                    id: linePopupScroll
                    anchors.fill: parent
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded

                    TextArea {
                        id: linePopupTextEdit
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                        textFormat: TextEdit.PlainText
                        text: root.popupLineText
                        width: Math.max(0, linePopupScroll.availableWidth)
                        color: "#2b251f"
                        font.family: UiSettings.logFontFamily
                        font.pixelSize: UiSettings.effectiveLogFontPixelSize
                        background: null
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: qsTr("Copy")
                    onClicked: root.popupCopyText()
                }

                Button {
                    text: qsTr("Close")
                    onClicked: linePopup.close()
                }
            }
        }
    }

    Frame {
        anchors.fill: parent
        padding: 0

        ListView {
            id: listView
            anchors.fill: parent
            clip: true
            model: root.rowModel
            boundsBehavior: Flickable.StopAtBounds
            interactive: !root.pointerSelectionActive
            rightMargin: verticalScrollBar.width
            contentWidth: root.wrapLogLines ? width : Math.max(width, root.maxRowWidth)
            footer: Item {
                width: listView.width
                height: horizontalScrollBar.height
            }
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
                height: Math.max(rowFontPixelSize, contentRow.implicitHeight) + 8
                color: root.isIndexSelected(index) ? "#d7e6f5" : root.rowBackgroundColor(index, logLevel)

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
                        x: 4
                        y: 4
                        spacing: 8

                        Label {
                            id: lineNumberLabel
                            text: lineNo > 0 ? lineNo : index + 1
                            color: Qt.darker(root.levelForegroundColor(logLevel), 1.1)
                            font.family: UiSettings.logFontFamily
                            font.pixelSize: rowFontPixelSize
                            renderType: Text.NativeRendering
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
                            renderType: Text.NativeRendering
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
                            root.openContextMenu(index, root.rowText(message, rawMessage),
                                mapToItem(root, mouse.x, mouse.y))
                            return
                        }

                        root.handlePressed(index, message, rawMessage, mouse.modifiers)
                    }

                    onPressAndHold: function(mouse) {
                        if (mouse.button !== Qt.LeftButton) {
                            return
                        }

                        root.finishPointerSelection()
                        const plainText = root.rowModel ? root.rowModel.plainTextAt(index) : root.rowText(message, rawMessage)
                        const point = mapToItem(root, mouse.x + 12, mouse.y + 12)
                        root.openLinePopup(index, plainText, point)
                    }

                    onClicked: function(mouse) {
                        if (mouse.button === Qt.LeftButton) {
                            if (linePopup.visible) {
                                return
                            }
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
        color: "slategray"
    }

    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: root.hasSelection
        onActivated: root.copySelectionToClipboard()
    }

    Shortcut {
        sequences: ["PgUp"]
        enabled: root.activeFocus
        onActivated: {
            root.pageScrollRequested(-1)
            root.scrollByPages(-1)
        }
    }

    Shortcut {
        sequences: ["PgDown"]
        enabled: root.activeFocus
        onActivated: {
            root.pageScrollRequested(1)
            root.scrollByPages(1)
        }
    }
}
