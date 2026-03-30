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
    property var filterModel: null
    property var claimedFilterModel: null
    readonly property bool hasSelection: lineList.hasSelection
    readonly property string selectedText: lineList.selectedText
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

    function releaseFilterModel() {
        if (!claimedFilterModel) {
            return
        }

        AppEngine.releaseFilterModel(claimedFilterModel)
        claimedFilterModel = null
        filterModel = null
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
        const point = viewMenuButton.mapToItem(null, 0, viewMenuButton.height)
        viewMenu.popup(point.x, point.y)
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

    onSourceUrlChanged: acquireFilterModel()
    onWorkspaceChanged: registerWithWorkspace()
    onFilterModelChanged: {
        filterField.text = filterModel ? filterModel.pattern : ""
    }
    Component.onCompleted: {
        acquireFilterModel()
        registerWithWorkspace()
    }
    Component.onDestruction: releaseFilterModel()

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
                    text: qsTr("regex")
                    checked: !!root.filterModel && root.filterModel.regex
                    onToggled: {
                        if (root.filterModel) {
                            root.filterModel.regex = checked
                        }
                    }
                }

                CheckBox {
                    text: qsTr("icase")
                    checked: !!root.filterModel && root.filterModel.caseInsensitive
                    onToggled: {
                        if (root.filterModel) {
                            root.filterModel.caseInsensitive = checked
                        }
                    }
                }

                CheckBox {
                    text: qsTr("auto-refresh")
                    checked: !!root.filterModel && root.filterModel.autoRefresh
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
