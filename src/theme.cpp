#include "theme.h"
#include "settings.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QRegularExpression>

Theme::Theme(Settings *settings, QObject *parent) : QObject(parent), m_settings(settings) {
    connect(settings, &Settings::fontChanged, this, &Theme::changed);
    m_dir = QDir::homePath() + "/.local/state/omarchy/current";
    m_file = m_dir + "/theme/colors.toml";
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, [this] { load(); watch(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    loadFont();
    load();
    watch();
}

void Theme::watch() {
    const QStringList old = m_watcher.files() + m_watcher.directories();
    if (!old.isEmpty()) m_watcher.removePaths(old);
    for (const QString &p : {m_dir, m_dir + "/theme", m_dir + "/theme.name", m_file})
        if (QFileInfo::exists(p)) m_watcher.addPath(p);
}

void Theme::loadFont() {
    m_fontFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    const QString home = QDir::homePath();
    struct Src { QString path; QRegularExpression rx; };
    const Src srcs[] = {
        {home + "/.config/ghostty/config", QRegularExpression(R"re(^\s*font-family\s*=\s*"?([^"\n]+)"?)re", QRegularExpression::MultilineOption)},
        {home + "/.config/kitty/kitty.conf", QRegularExpression(R"(^\s*font_family\s+(.+)$)", QRegularExpression::MultilineOption)},
        {home + "/.config/alacritty/alacritty.toml", QRegularExpression(R"re(normal\s*=\s*\{\s*family\s*=\s*"([^"]+)")re")},
    };
    for (const Src &s : srcs) {
        QFile f(s.path);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const auto m = s.rx.match(QString::fromUtf8(f.readAll()));
        if (m.hasMatch()) { m_fontFamily = m.captured(1).trimmed(); break; }
    }
}

QString Theme::fontFamily() const { return m_settings->fontFamily().isEmpty() ? m_fontFamily : m_settings->fontFamily(); }
qreal Theme::fontSize() const { return m_settings->fontSize() > 0 ? m_settings->fontSize() : 10; }
void Theme::zoom(int d) { m_settings->setFontSize(fontSize() + d); }
void Theme::resetZoom() { m_settings->setFontSize(0); }

QColor Theme::ansi(int i) const { return m_palette[qBound(0, i, 255)]; }

void Theme::load() {
    QHash<QString, QColor> c;
    QFile f(m_file);
    if (f.open(QIODevice::ReadOnly)) {
        static const QRegularExpression rx(R"re(^\s*([A-Za-z0-9_]+)\s*=\s*"(#[0-9A-Fa-f]{6})")re");
        for (const QByteArray &line : f.readAll().split('\n')) {
            const auto m = rx.match(QString::fromUtf8(line));
            if (m.hasMatch()) c.insert(m.captured(1), QColor(m.captured(2)));
        }
    }
    auto get = [&](const char *k, const QColor &d) { return c.value(k, d); };
    m_bg = get("background", m_bg);
    m_fg = get("foreground", m_fg);
    m_accent = get("accent", m_accent);
    m_muted = get("muted", get("color8", m_muted));
    m_panel = get("lighter_bg", m_panel);
    m_border = get("color8", m_border);
    m_red = get("red", m_red);
    m_green = get("green", m_green);
    m_yellow = get("yellow", m_yellow);
    m_blue = get("blue", m_blue);
    m_selection = get("selection_background", get("selection", m_accent));

    static const char *fallback[16] = {"#000000", "#cd0000", "#00cd00", "#cdcd00", "#0000ee", "#cd00cd", "#00cdcd", "#e5e5e5",
                                       "#7f7f7f", "#ff0000", "#00ff00", "#ffff00", "#5c5cff", "#ff00ff", "#00ffff", "#ffffff"};
    for (int i = 0; i < 16; ++i) m_palette[i] = c.value(QString("color%1").arg(i), QColor(fallback[i]));
    static const int lv[6] = {0, 95, 135, 175, 215, 255};
    for (int i = 16; i < 232; ++i) {
        int n = i - 16;
        m_palette[i] = QColor(lv[n / 36], lv[(n / 6) % 6], lv[n % 6]);
    }
    for (int i = 232; i < 256; ++i) { int g = 8 + (i - 232) * 10; m_palette[i] = QColor(g, g, g); }
    emit changed();
}
