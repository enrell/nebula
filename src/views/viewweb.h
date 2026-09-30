#pragma once
#include <QObject>

class QQuickWebEngineProfile;

// The web side of views: the `nebula-view://` scheme and the isolated browser profile view pages run in.
//   nebula-view://app/page.html|page.js|page.css|qwebchannel.js   renderer assets compiled into the binary
//   nebula-view://app/files/<view>/<relative path>                 a file the view's checked document references
// The profile is off the record (nothing written to disk) and blocks every request that is not nebula-view:,
// data: or blob:, so a view can never reach the network or arbitrary local files.
namespace ViewWeb {
void prepare();                        // before QtWebEngineQuick::initialize(): scheme registration, sandbox policy
QQuickWebEngineProfile *profile();     // created on first use (this is what starts Chromium), on the GUI thread

// Exposed to QML as `viewWeb`; asking for the profile only when a view pane appears keeps Chromium out of
// startup entirely for sessions that never show a view.
class Provider : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QObject *profile() const;
};
} // namespace ViewWeb
