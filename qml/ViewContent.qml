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
        onNavigationRequested: (request) => { if (request.url.toString() !== root.pane.pageUrl) request.reject() }
        onNewWindowRequested: (request) => {}
        onContextMenuRequested: (request) => { request.accepted = true }
        // page errors reach the terminal nebula was started from; the page also reports them to the view (view_get)
        onJavaScriptConsoleMessage: (level, message, line, source) => {
            if (level === WebEngineView.ErrorMessageLevel) console.warn(`view ${root.pane ? root.pane.id : "?"}: ${message} (${source}:${line})`)
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
