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
    property var markedModel: null
    property var claimedMarkedModel: null
    readonly property bool hasSelection: lineList.hasSelection
    readonly property string selectedText: lineList.selectedText

    function acquireMarkedModel() {
        releaseMarkedModel()
        if (!sourceUrl || sourceUrl.toString().length === 0) {
            return
        }

        const createdModel = AppEngine.createMarkedModel(sourceUrl)
        if (!createdModel) {
            console.error("Failed to create MarkedModel for source:", sourceUrl.toString())
            return
        }

        claimedMarkedModel = createdModel
        markedModel = createdModel
    }

    function releaseMarkedModel() {
        if (!claimedMarkedModel) {
            return
        }

        AppEngine.releaseMarkedModel(claimedMarkedModel)
        claimedMarkedModel = null
        markedModel = null
    }

    function activateView() {
        forceActiveFocus()
        if (workspace) {
            workspace.setActiveView(root, nodeId)
        }
    }

    function registerWithWorkspace() {
        if (workspace) {
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

    onSourceUrlChanged: acquireMarkedModel()
    onWorkspaceChanged: registerWithWorkspace()
    Component.onCompleted: {
        acquireMarkedModel()
        registerWithWorkspace()
    }
    Component.onDestruction: releaseMarkedModel()

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

                Label {
                    text: qsTr("Marked Lines")
                    color: "#4d453b"
                    font.bold: true
                }

                Item {
                    Layout.fillWidth: true
                }

                Label {
                    text: qsTr("%1 rows").arg(lineList.lineCount)
                    color: "#6c655c"
                }
            }
        }

        LogLineList {
            id: lineList
            Layout.fillWidth: true
            Layout.fillHeight: true
            rowModel: root.markedModel
            wrapLogLines: false
            emptyText: qsTr("No marked rows.")
            onActivated: root.activateView()
            onRowClicked: function(proxyRow, sourceRow) {
                root.activateView()
                if (root.workspace) {
                    root.workspace.revealSourceRowInPrimaryLog(sourceRow)
                }
            }
        }
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

        Menu {
            title: qsTr("Close")

            MenuItem {
                text: qsTr("This Pane")
                enabled: !!root.workspace && root.nodeId >= 0
                onTriggered: {
                    if (root.workspace) {
                        root.workspace.closePane(root.nodeId)
                    }
                }
            }
        }
    }
}
