.pragma library

const COMPONENT_STATUS_NULL = 0
const COMPONENT_STATUS_READY = 1
const COMPONENT_STATUS_LOADING = 2
const COMPONENT_STATUS_ERROR = 3

function openDialog(source, parent, properties) {
    if (!source || !parent) {
        console.error("DialogHelper.openDialog requires both source and parent", source, parent)
        return null
    }

    const component = Qt.createComponent(source, parent)
    if (!component) {
        console.error("Failed to create dialog component handle:", source)
        return null
    }

    function createAndOpenDialog() {
        const dialog = component.createObject(parent, properties || {})
        if (!dialog) {
            console.error("Failed to create dialog instance:", source, component.errorString())
            return null
        }

        if (typeof dialog.open !== "function") {
            console.error("Created object is not an openable dialog:", source)
            dialog.destroy()
            return null
        }

        dialog.closed.connect(function() {
            dialog.destroy()
        })

        dialog.open()
        return dialog
    }

    if (component.status === COMPONENT_STATUS_READY) {
        return createAndOpenDialog()
    }

    if (component.status === COMPONENT_STATUS_LOADING) {
        component.statusChanged.connect(function() {
            if (component.status === COMPONENT_STATUS_READY) {
                createAndOpenDialog()
            } else if (component.status === COMPONENT_STATUS_ERROR) {
                console.error("Failed to load dialog component:", source, component.errorString())
            }
        })
        return null
    }

    if (component.status === COMPONENT_STATUS_ERROR) {
        console.error("Failed to load dialog component:", source, component.errorString())
        return null
    }

    console.error("Dialog component has invalid status:", source, component.status)
    return null
}
