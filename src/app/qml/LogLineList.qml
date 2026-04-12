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
    property real lastVerticalScrollBarPosition: 0
    readonly property bool hasSelection: selectedIndexes.length > 0
    readonly property real verticalScrollBarReserve: 14
    readonly property real horizontalScrollBarReserve: 16
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
    signal zoomWheelRequested(int steps)
    signal backwardWheelScrollRequested()
    signal upwardScrollBarNavigationRequested()

    TextMetrics {
        id: horizontalMeasure
        font.family: UiSettings.logFontFamily
        font.pixelSize: UiSettings.effectiveLogFontPixelSize
    }

    function normalizeIndex(index) {
        return Math.max(0, Math.min(index, listView.count - 1))
    }

    function maximumContentX() {
        return Math.max(0, listView.contentWidth - listView.width)
    }

    function selectedAnchorIndex() {
        if (selectedIndexes.length > 0) {
            return selectedIndexes[selectedIndexes.length - 1]
        }

        return topVisibleIndex
    }

    function rowText(message, rawMessage) {
        return message.length > 0 ? message : rawMessage
    }

    function measuredRowWidth(index) {
        if (!rowModel || index < 0 || index >= listView.count) {
            return 0
        }

        const messageText = rowModel.plainTextAt(index)
        const lineNumberText = String(rowModel.lineNoAt(index))
        horizontalMeasure.text = lineNumberText
        const lineNumberWidth = horizontalMeasure.advanceWidth
        horizontalMeasure.text = messageText
        const messageWidth = horizontalMeasure.advanceWidth
        return UiSettings.effectiveLogFontPixelSize + 4 + lineNumberWidth + 8 + messageWidth + 18
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

    function levelLabel(logLevel) {
        switch (logLevel) {
        case 0:
            return qsTr("ERROR")
        case 1:
            return qsTr("WARN")
        case 2:
            return qsTr("NOTICE")
        case 3:
            return qsTr("INFO")
        case 4:
            return qsTr("DEBUG")
        case 5:
            return qsTr("TRACE")
        default:
            return ""
        }
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

    function popupTextForRow(rowIndex, fallbackText) {
        if (!rowModel || rowIndex < 0 || rowIndex >= listView.count) {
            return fallbackText ? fallbackText : ""
        }

        const rawText = rowModel.rawTextAt(rowIndex)
        return rawText.length > 0 ? rawText : (fallbackText ? fallbackText : "")
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

    function scrollHorizontally(deltaPixels) {
        if (wrapLogLines || deltaPixels === 0) {
            return
        }

        listView.contentX = Math.max(0, Math.min(maximumContentX(), listView.contentX + deltaPixels))
    }

    function scrollToLineStart() {
        if (wrapLogLines) {
            return
        }

        listView.contentX = 0
    }

    function scrollToSelectedLineEnd() {
        if (wrapLogLines) {
            return
        }

        const anchorIndex = selectedAnchorIndex()
        const rowWidth = measuredRowWidth(anchorIndex)
        const targetContentX = Math.max(0, rowWidth - listView.width)
        listView.contentX = Math.max(0, Math.min(maximumContentX(), targetContentX))
    }

    function scrollByWheelEvent(wheel) {
        const pixelDeltaY = wheel.pixelDelta.y
        if (pixelDeltaY !== 0) {
            if (pixelDeltaY > 0) {
                root.backwardWheelScrollRequested()
            }
            const maximumContentY = Math.max(0, listView.contentHeight - listView.height)
            if (maximumContentY <= 0) {
                return
            }

            listView.contentY = Math.max(0, Math.min(maximumContentY, listView.contentY - pixelDeltaY))
            updateTopVisibleIndex()
            return
        }

        const angleDeltaY = wheel.angleDelta.y
        if (angleDeltaY === 0) {
            return
        }

        if (angleDeltaY > 0) {
            root.backwardWheelScrollRequested()
        }

        const notchCount = angleDeltaY / 120.0
        const rowsPerNotch = 3
        scrollByRows(-Math.round(notchCount * rowsPerNotch))
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

        MenuItem {
            text: qsTr("Copy Line")
            enabled: root.contextMenuLineIndex >= 0
            onTriggered: root.copyLineToClipboard(root.contextMenuLineIndex)
        }

        MenuItem {
            text: qsTr("Copy Selection")
            enabled: root.hasSelection
            onTriggered: root.copySelectionToClipboard()
        }

        MenuSeparator {}

        MenuItem {
            text: qsTr("Show")
            enabled: root.contextMenuLineIndex >= 0
            onTriggered: {
                const point = Qt.point(Math.round((root.width - linePopup.width) / 2),
                                       Math.round((root.height - linePopup.height) / 2))
                root.openLinePopup(root.contextMenuLineIndex,
                                   root.popupTextForRow(root.contextMenuLineIndex, root.contextMenuLineText),
                                   point)
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

        Rectangle {
            anchors.fill: parent
            color: "#f6f3ee"
        }

        ListView {
            id: listView
            anchors.fill: parent
            anchors.rightMargin: root.verticalScrollBarReserve
            anchors.bottomMargin: root.horizontalScrollBarReserve
            clip: true
            model: root.rowModel
            boundsBehavior: Flickable.StopAtBounds
            interactive: !root.pointerSelectionActive
            rightMargin: 8
            contentWidth: root.wrapLogLines ? width : Math.max(width, root.maxRowWidth)
            footer: Item {
                width: listView.width
                height: 0
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
                parent: root
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: root.horizontalScrollBarReserve
                active: true
                policy: ScrollBar.AlwaysOn
                width: root.verticalScrollBarReserve
                z: 20

                onPressedChanged: {
                    if (pressed) {
                        root.lastVerticalScrollBarPosition = position
                    }
                }

                onPositionChanged: {
                    if (pressed && position < root.lastVerticalScrollBarPosition - 0.0001) {
                        root.upwardScrollBarNavigationRequested()
                    }
                    root.lastVerticalScrollBarPosition = position
                }
            }

            ScrollBar.horizontal: ScrollBar {
                id: horizontalScrollBar
                parent: root
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.rightMargin: root.verticalScrollBarReserve
                anchors.bottom: parent.bottom
                active: true
                policy: ScrollBar.AlwaysOn
                height: root.horizontalScrollBarReserve
                z: 20
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
                required property date date
                readonly property int rowFontPixelSize: UiSettings.effectiveLogFontPixelSize
                readonly property real intrinsicRowWidth: contentRow.implicitWidth + gutter.width + 18
                readonly property bool hasTimestamp: date && date.toString().length > 0
                readonly property bool showLevelBadge: !!root.rowModel
                    && root.rowModel.scannerName !== "None"
                readonly property string formattedTimestamp: hasTimestamp
                    ? date.toString("yyyy-MM-dd HH:mm:ss.zzz")
                    : ""

                width: ListView.view
                    ? (root.wrapLogLines
                        ? ListView.view.width
                        : Math.max(ListView.view.width,
                                   root.maxRowWidth,
                                   intrinsicRowWidth))
                    : Math.max(root.maxRowWidth, intrinsicRowWidth)
                height: Math.max(rowFontPixelSize, contentRow.implicitHeight) + 8
                color: "transparent"

                Component.onCompleted: {
                    if (intrinsicRowWidth > root.maxRowWidth) {
                        root.maxRowWidth = intrinsicRowWidth
                    }
                }

                onIntrinsicRowWidthChanged: {
                    if (intrinsicRowWidth > root.maxRowWidth) {
                        root.maxRowWidth = intrinsicRowWidth
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    color: root.isIndexSelected(index) ? "#d7e6f5" : root.rowBackgroundColor(index, logLevel)
                    z: -1
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
                    clip: true

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

                        Rectangle {
                            id: levelBadge
                            visible: rowDelegate.showLevelBadge
                            radius: 4
                            color: Qt.darker(root.levelBackgroundColor(logLevel), 1.08)
                            border.width: 1
                            border.color: Qt.darker(root.levelForegroundColor(logLevel), 1.05)
                            implicitWidth: levelBadgeLabel.implicitWidth + 10
                            implicitHeight: Math.max(rowFontPixelSize, levelBadgeLabel.implicitHeight + 4)

                            Label {
                                id: levelBadgeLabel
                                anchors.centerIn: parent
                                text: root.levelLabel(logLevel)
                                color: root.levelForegroundColor(logLevel)
                                font.family: UiSettings.logFontFamily
                                font.pixelSize: Math.max(10, rowFontPixelSize - 1)
                                font.bold: true
                                renderType: Text.NativeRendering
                            }
                        }

                        Label {
                            id: timestampLabel
                            visible: rowDelegate.hasTimestamp
                            text: rowDelegate.formattedTimestamp
                            color: Qt.darker(root.levelForegroundColor(logLevel), 1.05)
                            font.family: UiSettings.logFontFamily
                            font.pixelSize: rowFontPixelSize
                            renderType: Text.NativeRendering
                        }

                        Label {
                            id: messageLabel
                            text: message.length > 0 ? message : rawMessage
                            width: root.wrapLogLines
                                ? Math.max(0, contentArea.width
                                              - lineNumberLabel.implicitWidth
                                              - (levelBadge.visible ? levelBadge.implicitWidth : 0)
                                              - (timestampLabel.visible ? timestampLabel.implicitWidth : 0)
                                              - (contentRow.spacing * ((levelBadge.visible ? 1 : 0)
                                                                       + (timestampLabel.visible ? 1 : 0)
                                                                       + 1))
                                              - 12)
                                : implicitWidth
                            wrapMode: root.wrapLogLines ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
                            elide: root.wrapLogLines ? Text.ElideNone : Text.ElideLeft
                            clip: true
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
                        const rawText = root.popupTextForRow(index, rawMessage.length > 0 ? rawMessage : root.rowText(message, rawMessage))
                        const point = mapToItem(root, mouse.x + 12, mouse.y + 12)
                        root.openLinePopup(index, rawText, point)
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

        MouseArea {
            anchors.fill: parent
            anchors.rightMargin: root.verticalScrollBarReserve
            anchors.bottomMargin: root.horizontalScrollBarReserve
            acceptedButtons: Qt.NoButton
            propagateComposedEvents: false
            z: 5

            onWheel: function(wheel) {
                if ((wheel.modifiers & Qt.ControlModifier) !== 0) {
                    const deltaY = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.pixelDelta.y
                    if (deltaY !== 0) {
                        root.zoomWheelRequested(deltaY > 0 ? 1 : -1)
                    }
                    wheel.accepted = true
                    return
                }

                root.scrollByWheelEvent(wheel)
                wheel.accepted = true
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            width: root.verticalScrollBarReserve
            height: root.horizontalScrollBarReserve
            color: "#f6f3ee"
            z: 19
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
