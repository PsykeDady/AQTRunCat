// AQTRunCat — M1.0 tray spike.
//
// Throwaway prototype answering one question: is QSystemTrayIcon good enough to
// show and animate a RunCat Neo compatible runner on KDE Plasma 6 + Wayland?
// It animates a fixed runner at a fixed, user-chosen FPS. Nothing here is meant
// to survive into the real application.

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QIcon>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QSystemTrayIcon>
#include <QTimer>

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int signalPipe[2] = {-1, -1};

void onUnixSignal(int)
{
    const char c = 1;
    [[maybe_unused]] auto n = ::write(signalPipe[1], &c, 1);
}

// Frames are named <runner>-frame-<N>.png; sort by N, not alphabetically.
QList<QImage> loadFrames(const QString &runner)
{
    const QDir dir(QStringLiteral(":/runners/") + runner);
    static const QRegularExpression re(QStringLiteral("-frame-(\\d+)\\.png$"));
    QList<std::pair<int, QString>> files;
    for (const QString &name : dir.entryList({QStringLiteral("*.png")}, QDir::Files)) {
        const auto m = re.match(name);
        if (m.hasMatch())
            files.append({m.captured(1).toInt(), dir.filePath(name)});
    }
    std::sort(files.begin(), files.end());
    QList<QImage> frames;
    for (const auto &[index, path] : files)
        frames.append(QImage(path).convertToFormat(QImage::Format_ARGB32_Premultiplied));
    return frames;
}

// Monochrome ("template") semantics: keep the alpha, replace the colour.
QImage tinted(const QImage &frame, const QColor &color)
{
    QImage out = frame;
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(out.rect(), color);
    return out;
}

class PaletteWatcher : public QObject
{
public:
    std::function<void()> onChange;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange && onChange)
            onChange();
        return false;
    }
};

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("AQTRunCat"));
    QApplication::setApplicationVersion(QStringLiteral("0.0.1-spike"));
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AQTRunCat M1.0 tray spike"));
    parser.addHelpOption();
    const QCommandLineOption fpsOpt(QStringLiteral("fps"), QStringLiteral("Animation frames per second."), QStringLiteral("fps"), QStringLiteral("10"));
    const QCommandLineOption runnerOpt(QStringLiteral("runner"), QStringLiteral("cat, coffee or engine."), QStringLiteral("name"), QStringLiteral("cat"));
    const QCommandLineOption tintOpt(QStringLiteral("tint"),
                                     QStringLiteral("'text' (palette text colour), 'none' (raw frames) or a colour such as #ff00ff."),
                                     QStringLiteral("tint"), QStringLiteral("text"));
    const QCommandLineOption durationOpt(QStringLiteral("duration"), QStringLiteral("Quit after N seconds (0 = run until quit)."), QStringLiteral("s"), QStringLiteral("0"));
    const QCommandLineOption statsOpt(QStringLiteral("stats"), QStringLiteral("Print timing statistics every N seconds (0 = off)."), QStringLiteral("s"), QStringLiteral("5"));
    parser.addOptions({fpsOpt, runnerOpt, tintOpt, durationOpt, statsOpt});
    parser.process(app);

    const double fps = parser.value(fpsOpt).toDouble();
    if (fps <= 0 || fps > 1000) {
        std::fprintf(stderr, "invalid --fps\n");
        return 2;
    }
    const QString runner = parser.value(runnerOpt);
    const QString tintMode = parser.value(tintOpt);

    const QList<QImage> frames = loadFrames(runner);
    if (frames.isEmpty()) {
        std::fprintf(stderr, "no frames for runner '%s'\n", qPrintable(runner));
        return 2;
    }
    std::printf("runner=%s frames=%lld size=%dx%d fps=%.1f interval=%dms tint=%s\n",
                qPrintable(runner), static_cast<long long>(frames.size()),
                frames.first().width(), frames.first().height(), fps, qRound(1000.0 / fps),
                qPrintable(tintMode));

    if (!QSystemTrayIcon::isSystemTrayAvailable())
        std::fprintf(stderr, "warning: no system tray available (yet)\n");

    // Build every icon once; the timer only swaps prebuilt QIcons.
    QList<QIcon> icons;
    auto buildIcons = [&] {
        QColor color;
        if (tintMode == QLatin1String("text"))
            color = QApplication::palette().color(QPalette::Active, QPalette::WindowText);
        else if (tintMode != QLatin1String("none"))
            color = QColor::fromString(tintMode);
        icons.clear();
        for (const QImage &frame : frames)
            icons.append(QIcon(QPixmap::fromImage(color.isValid() ? tinted(frame, color) : frame)));
        std::printf("icons built, tint colour=%s\n", color.isValid() ? qPrintable(color.name()) : "none");
        std::fflush(stdout);
    };
    buildIcons();

    QSystemTrayIcon tray;
    QMenu menu;
    menu.addAction(QStringLiteral("Quit AQTRunCat spike"), &app, &QApplication::quit);
    tray.setContextMenu(&menu);
    tray.setToolTip(QStringLiteral("AQTRunCat spike: %1 @ %2 fps").arg(runner).arg(fps));
    tray.setIcon(icons.first());
    tray.show();

    PaletteWatcher paletteWatcher;
    if (tintMode == QLatin1String("text")) {
        paletteWatcher.onChange = [&] {
            std::printf("palette changed\n");
            buildIcons();
        };
        app.installEventFilter(&paletteWatcher);
    }

    // Animation and its timing statistics.
    qsizetype frameIndex = 0;
    QElapsedTimer sinceLastTick, total, window;
    qint64 ticks = 0, windowTicks = 0, windowMaxDtNs = 0, windowSetIconNs = 0;
    QTimer animation;
    animation.setTimerType(Qt::PreciseTimer);
    animation.setInterval(qRound(1000.0 / fps));
    QObject::connect(&animation, &QTimer::timeout, [&] {
        windowMaxDtNs = std::max(windowMaxDtNs, sinceLastTick.nsecsElapsed());
        sinceLastTick.start();
        frameIndex = (frameIndex + 1) % icons.size();
        QElapsedTimer cost;
        cost.start();
        tray.setIcon(icons[frameIndex]);
        windowSetIconNs += cost.nsecsElapsed();
        ++ticks;
        ++windowTicks;
    });

    QTimer stats;
    const int statsSeconds = parser.value(statsOpt).toInt();
    QObject::connect(&stats, &QTimer::timeout, [&] {
        const double seconds = window.nsecsElapsed() / 1e9;
        std::printf("[stats] t=%.0fs ticks=%lld rate=%.2f/s max_dt=%.1fms setIcon_avg=%.1fus\n",
                    total.nsecsElapsed() / 1e9, static_cast<long long>(windowTicks), windowTicks / seconds,
                    windowMaxDtNs / 1e6, windowTicks ? windowSetIconNs / 1e3 / windowTicks : 0.0);
        std::fflush(stdout);
        windowTicks = windowMaxDtNs = windowSetIconNs = 0;
        window.start();
    });

    // Clean shutdown on SIGINT/SIGTERM through the self-pipe trick.
    ::socketpair(AF_UNIX, SOCK_STREAM, 0, signalPipe);
    QSocketNotifier signalNotifier(signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, &app, &QApplication::quit);
    std::signal(SIGINT, onUnixSignal);
    std::signal(SIGTERM, onUnixSignal);

    if (const int duration = parser.value(durationOpt).toInt(); duration > 0)
        QTimer::singleShot(duration * 1000, &app, &QApplication::quit);

    total.start();
    window.start();
    sinceLastTick.start();
    animation.start();
    if (statsSeconds > 0)
        stats.start(statsSeconds * 1000);

    const int rc = app.exec();
    animation.stop();
    tray.hide();
    std::printf("exit: %lld frames in %.1fs (%.2f/s)\n", static_cast<long long>(ticks),
                total.nsecsElapsed() / 1e9, ticks / (total.nsecsElapsed() / 1e9));
    return rc;
}
