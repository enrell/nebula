import QtQuick
import QtWebChannel
import QtWebEngine

// The body of a view pane: the renderer page in an isolated web engine profile, fed by its ViewPane over
// QWebChannel. The page never navigates; links go to ViewPane.openLink (system browser, http/https/mailto only).
Item {
    id: root
    property var pane                 // ViewPane
    property bool paneFocused: false
    signal activated()

    onPaneFocusedChanged: if (paneFocused) web.forceActiveFocus()

    WebChannel { id: channel }

    // export and snapshot requests (ViewPane): the page writes HTML itself; printing and grabbing happen here
    property int pdfRequest: 0
    Connections {
        target: root.pane
        // the page first swaps live canvases for print stills, then asks for the print
        function onPrintRequested(request, path) {
            if (root.pdfRequest) { root.pane.exportFailed(request, "another PDF export of this view is still running"); return }
            root.pdfRequest = request
            web.printToPdf(path)
        }
        function onGrabRequested(request) {
            if (!web.grabToImage((result) => root.pane.snapshotTaken(request, result.image)))
                root.pane.snapshotTaken(request, null)
        }
    }

    WebEngineView {
        id: web
        anchors.fill: parent
        profile: viewWeb.profile()
        backgroundColor: theme.bg
        webChannel: channel
        settings.javascriptCanOpenWindows: false
        settings.localContentCanAccessRemoteUrls: false
        settings.localContentCanAccessFileUrls: false
        settings.pluginsEnabled: false
        settings.focusOnNavigationEnabled: false
        settings.showScrollBars: true
        // the page itself never navigates; sub-frames may only load the view's own html sandboxes
        onNavigationRequested: (request) => {
            const url = request.url.toString()
            const allowed = request.isMainFrame ? url === root.pane.pageUrl : url.startsWith(root.pane.sandboxBase)
            if (!allowed) request.reject()
        }
        onNewWindowRequested: (request) => {}
        onContextMenuRequested: (request) => { request.accepted = true }
        // page errors reach the terminal nebula was started from; the page also reports them to the view (view_get)
        onJavaScriptConsoleMessage: (level, message, line, source) => {
            // errors of agent code in html sandboxes are the agent's: they reach it as render issues, not this log
            if (level === WebEngineView.ErrorMessageLevel && !source.startsWith(root.pane.sandboxBase)) console.warn(`view ${root.pane ? root.pane.id : "?"}: ${message} (${source}:${line})`)
        }
        onPdfPrintingFinished: (filePath, success) => {
            const request = root.pdfRequest
            root.pdfRequest = 0
            if (request) root.pane.pdfFinished(request, success)
        }
        onLoadingChanged: (info) => { if (info.status === WebEngineView.LoadFailedStatus) console.warn(`view page failed to load: ${info.errorString}`) }

        TapHandler {
            gesturePolicy: TapHandler.DragThreshold
            onPressedChanged: if (pressed) root.activated()
        }
    }

    Component.onCompleted: {
        if (!pane) return
        channel.registerObject("view", pane)
        web.url = pane.pageUrl
    }
    Component.onDestruction: if (pane) pane.pageDetached()
}
