import QtQuick
import QtQuick.Controls

Item {
    id: root

    property url sourceUrl
    property var activeView: null
    property int activeNodeId: -1
    property var primaryLogView: null
    property int nextNodeId: 1
    property var layoutRoot: null

    function createLeaf(viewType) {
        return {
            kind: "leaf",
            id: nextNodeId++,
            viewType: viewType
        }
    }

    function firstLeafId(node) {
        if (!node) {
            return -1
        }

        if (node.kind === "leaf") {
            return node.id
        }

        const firstId = firstLeafId(node.first)
        return firstId >= 0 ? firstId : firstLeafId(node.second)
    }

    function cloneNode(node) {
        if (!node) {
            return null
        }

        if (node.kind === "leaf") {
            return {
                kind: "leaf",
                id: node.id,
                viewType: node.viewType
            }
        }

        return {
            kind: "split",
            id: node.id,
            orientation: node.orientation,
            initialFirstRatio: node.initialFirstRatio,
            initialSecondRatio: node.initialSecondRatio,
            initialFirstSize: node.initialFirstSize,
            initialSecondSize: node.initialSecondSize,
            first: cloneNode(node.first),
            second: cloneNode(node.second)
        }
    }

    function splitLeaf(node, targetId, orientation, newViewType, initialExtent) {
        if (!node) {
            return null
        }

        if (node.kind === "leaf") {
            if (node.id !== targetId) {
                return cloneNode(node)
            }

            return {
                kind: "split",
                id: nextNodeId++,
                orientation: orientation,
                initialFirstRatio: 0.5,
                initialSecondRatio: 0.5,
                initialFirstSize: Math.max(0, initialExtent * 0.5),
                initialSecondSize: Math.max(0, initialExtent * 0.5),
                first: cloneNode(node),
                second: createLeaf(newViewType)
            }
        }

        return {
            kind: "split",
            id: node.id,
            orientation: node.orientation,
            initialFirstRatio: node.initialFirstRatio,
            initialSecondRatio: node.initialSecondRatio,
            initialFirstSize: node.initialFirstSize,
            initialSecondSize: node.initialSecondSize,
            first: splitLeaf(node.first, targetId, orientation, newViewType, initialExtent),
            second: splitLeaf(node.second, targetId, orientation, newViewType, initialExtent)
        }
    }

    function closeLeaf(node, targetId) {
        if (!node) {
            return null
        }

        if (node.kind === "leaf") {
            return node.id === targetId ? null : cloneNode(node)
        }

        const nextFirst = closeLeaf(node.first, targetId)
        const nextSecond = closeLeaf(node.second, targetId)
        if (!nextFirst && !nextSecond) {
            return null
        }
        if (!nextFirst) {
            return nextSecond
        }
        if (!nextSecond) {
            return nextFirst
        }

        return {
            kind: "split",
            id: node.id,
            orientation: node.orientation,
            initialFirstRatio: node.initialFirstRatio,
            initialSecondRatio: node.initialSecondRatio,
            initialFirstSize: node.initialFirstSize,
            initialSecondSize: node.initialSecondSize,
            first: nextFirst,
            second: nextSecond
        }
    }

    function resetLayout() {
        nextNodeId = 1
        primaryLogView = null
        activeView = null
        layoutRoot = createLeaf("log")
        activeNodeId = layoutRoot.id
    }

    function registerPrimaryLogView(view) {
        primaryLogView = view
    }

    function setActiveView(view, nodeId) {
        activeView = view
        activeNodeId = nodeId
    }

    function openNewView(viewType, orientation) {
        if (!layoutRoot) {
            resetLayout()
        }

        const targetNodeId = activeNodeId > 0 ? activeNodeId : layoutRoot.id
        const targetExtent = activeView
            ? (orientation === Qt.Horizontal ? activeView.width : activeView.height)
            : 0
        layoutRoot = splitLeaf(layoutRoot, targetNodeId, orientation, viewType, targetExtent)
    }

    function openNewFilterView(orientation) {
        openNewView("filter", orientation)
    }

    function openNewMarkedView(orientation) {
        openNewView("marked", orientation)
    }

    function closePane(nodeId) {
        if (!layoutRoot || nodeId < 0 || nodeId === layoutRoot.id) {
            return
        }

        primaryLogView = null
        activeView = null
        layoutRoot = closeLeaf(layoutRoot, nodeId)
        activeNodeId = firstLeafId(layoutRoot)
    }

    function revealSourceRowInPrimaryLog(sourceRow) {
        if (!primaryLogView || sourceRow < 0) {
            return
        }

        primaryLogView.revealSourceRow(sourceRow, !primaryLogView.following)
    }

    function assignNode(loader, workspace, nodeData) {
        if (!loader || !loader.item) {
            return
        }

        loader.item.workspace = workspace
        loader.item.nodeData = nodeData
    }

    onSourceUrlChanged: resetLayout()
    Component.onCompleted: {
        if (!layoutRoot) {
            resetLayout()
        }
    }

    Loader {
        id: rootNodeLoader
        anchors.fill: parent
        sourceComponent: nodeComponent
        property var workspace: root
        property var nodeData: root.layoutRoot
        onLoaded: root.assignNode(rootNodeLoader, workspace, nodeData)
        onWorkspaceChanged: root.assignNode(rootNodeLoader, workspace, nodeData)
        onNodeDataChanged: root.assignNode(rootNodeLoader, workspace, nodeData)
    }

    Component {
        id: nodeComponent

        Item {
            id: nodeRoot

            property var workspace: null
            property var nodeData: null

            Loader {
                anchors.fill: parent
                sourceComponent: nodeRoot.nodeData && nodeRoot.nodeData.kind === "split" ? splitComponent : leafComponent
            }

            Component {
                id: splitComponent

                SplitView {
                    id: splitView
                    anchors.fill: parent
                    orientation: nodeRoot.nodeData.orientation
                    property bool initialSizesApplied: false

                    function applyInitialSizes() {
                        if (initialSizesApplied || !nodeRoot.nodeData) {
                            return
                        }

                        const firstSize = Number(nodeRoot.nodeData.initialFirstSize)
                        const secondSize = Number(nodeRoot.nodeData.initialSecondSize)
                        const firstRatio = Number(nodeRoot.nodeData.initialFirstRatio)
                        const secondRatio = Number(nodeRoot.nodeData.initialSecondRatio)
                        if (!Number.isFinite(firstSize) && (!Number.isFinite(firstRatio) || !Number.isFinite(secondRatio))) {
                            initialSizesApplied = true
                            return
                        }

                        if (orientation === Qt.Horizontal) {
                            const availableWidth = width - handle.implicitWidth
                            const resolvedFirstWidth = Number.isFinite(firstSize) ? firstSize : availableWidth * firstRatio
                            const resolvedSecondWidth = Number.isFinite(secondSize) ? secondSize : availableWidth * secondRatio
                            if (resolvedFirstWidth <= 0 || resolvedSecondWidth <= 0) {
                                return
                            }

                            firstPane.SplitView.fillWidth = false
                            secondPane.SplitView.fillWidth = false
                            firstPane.SplitView.preferredWidth = Math.max(0, resolvedFirstWidth)
                            secondPane.SplitView.preferredWidth = Math.max(0, resolvedSecondWidth)
                        } else {
                            const availableHeight = height - handle.implicitHeight
                            const resolvedFirstHeight = Number.isFinite(firstSize) ? firstSize : availableHeight * firstRatio
                            const resolvedSecondHeight = Number.isFinite(secondSize) ? secondSize : availableHeight * secondRatio
                            if (resolvedFirstHeight <= 0 || resolvedSecondHeight <= 0) {
                                return
                            }

                            firstPane.SplitView.fillHeight = false
                            secondPane.SplitView.fillHeight = false
                            firstPane.SplitView.preferredHeight = Math.max(0, resolvedFirstHeight)
                            secondPane.SplitView.preferredHeight = Math.max(0, resolvedSecondHeight)
                        }

                        initialSizesApplied = true
                    }

                    onWidthChanged: applyInitialSizes()
                    onHeightChanged: applyInitialSizes()
                    Component.onCompleted: applyInitialSizes()

                    handle: Rectangle {
                        id: handle
                        implicitWidth: nodeRoot.nodeData.orientation === Qt.Horizontal ? 6 : 1
                        implicitHeight: nodeRoot.nodeData.orientation === Qt.Horizontal ? 1 : 6
                        color: "#d5cec3"
                    }

                    Item {
                        id: firstPane
                        SplitView.minimumWidth: 120
                        SplitView.minimumHeight: 80

                        Loader {
                            id: firstNodeLoader
                            anchors.fill: parent
                            sourceComponent: nodeComponent
                            property var workspace: nodeRoot.workspace
                            property var nodeData: nodeRoot.nodeData.first
                            onLoaded: root.assignNode(firstNodeLoader, workspace, nodeData)
                            onWorkspaceChanged: root.assignNode(firstNodeLoader, workspace, nodeData)
                            onNodeDataChanged: root.assignNode(firstNodeLoader, workspace, nodeData)
                        }
                    }

                    Item {
                        id: secondPane
                        SplitView.minimumWidth: 120
                        SplitView.minimumHeight: 80

                        Loader {
                            id: secondNodeLoader
                            anchors.fill: parent
                            sourceComponent: nodeComponent
                            property var workspace: nodeRoot.workspace
                            property var nodeData: nodeRoot.nodeData.second
                            onLoaded: root.assignNode(secondNodeLoader, workspace, nodeData)
                            onWorkspaceChanged: root.assignNode(secondNodeLoader, workspace, nodeData)
                            onNodeDataChanged: root.assignNode(secondNodeLoader, workspace, nodeData)
                        }
                    }
                }
            }

            Component {
                id: leafComponent

                Loader {
                    anchors.fill: parent
                    sourceComponent: {
                        if (!nodeRoot.nodeData) {
                            return logViewComponent
                        }
                        if (nodeRoot.nodeData.viewType === "filter") {
                            return filterViewComponent
                        }
                        if (nodeRoot.nodeData.viewType === "marked") {
                            return markedViewComponent
                        }
                        return logViewComponent
                    }
                }
            }

            Component {
                id: logViewComponent

                LogView {
                    sourceUrl: nodeRoot.workspace ? nodeRoot.workspace.sourceUrl : ""
                    workspace: nodeRoot.workspace
                    nodeId: nodeRoot.nodeData ? nodeRoot.nodeData.id : -1
                    primaryView: true
                }
            }

            Component {
                id: filterViewComponent

                FilterView {
                    sourceUrl: nodeRoot.workspace ? nodeRoot.workspace.sourceUrl : ""
                    workspace: nodeRoot.workspace
                    nodeId: nodeRoot.nodeData ? nodeRoot.nodeData.id : -1
                }
            }

            Component {
                id: markedViewComponent

                MarkedView {
                    sourceUrl: nodeRoot.workspace ? nodeRoot.workspace.sourceUrl : ""
                    workspace: nodeRoot.workspace
                    nodeId: nodeRoot.nodeData ? nodeRoot.nodeData.id : -1
                }
            }
        }
    }
}
