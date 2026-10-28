#pragma once

#include "boundedprocess.h"
#include "metrics/packageupdates.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <optional>

// F5.4 / F5.5: the backend's update checks. Distribution packages (pacman,
// apt or dnf, detected from what is installed) and Flatpak are two
// independent jobs; each runs at most one bounded, read-only command at a
// time, never overlaps itself, and is scheduled `interval` after its
// previous check finished. Cached metadata only, unless the user enabled the
// remote refresh. A change of the package databases (an upgrade, a refresh
// by the system) triggers one debounced re-check.
class UpdatesService : public QObject
{
    Q_OBJECT

public:
    struct Options {
        QStringList searchPath;      // directories for the tools; empty: $PATH
        QString root;                // prefix of /etc, /var ("" = the real root)
        QString home;                // for Flatpak's user installation; empty: $HOME
        QString checkupdatesDatabase; // test override; empty: CHECKUPDATES_DB or checkupdates default
        int firstCheckDelayMs = 30000;
        int policyChangeDelayMs = 2000;
        int changeDebounceMs = 30000;
        int timeoutMs = 120000;
        qsizetype maxOutputBytes = 4 * 1024 * 1024;
        std::function<qint64()> now; // epoch seconds; empty: the system clock
    };

    explicit UpdatesService(Options options, QObject *parent = nullptr);
    ~UpdatesService() override;

    // intervalMinutes 0 turns the checks off; remote refreshes metadata
    // first where that is possible without root (never more often than
    // hourly).
    void setPolicy(int intervalMinutes, bool remote);
    void start();
    void stop();

    const aqtruncat::metrics::UpdatesMetrics &packages() const { return m_packages.metrics; }
    const aqtruncat::metrics::UpdatesMetrics &flatpak() const { return m_flatpak.metrics; }

signals:
    void changed();

private:
    struct Command {
        QString program;
        QStringList arguments;
        QString installation; // Flatpak: which installation
        bool runtimes = false; // Flatpak: --runtime instead of --app
    };
    struct Job {
        aqtruncat::metrics::UpdatesMetrics metrics;
        BoundedProcess process;
        QTimer due;
        QTimer debounce;
        QList<Command> queue;  // Flatpak runs several commands per check
        QList<aqtruncat::metrics::FlatpakInstallationCount> partial;
        bool rerun = false;    // a change arrived while checking
        bool remote = false;
        aqtruncat::metrics::PackageManager manager = aqtruncat::metrics::PackageManager::None;
        QStringList watched;
    };

    void schedule(Job &job, int delayMs);
    void checkPackages();
    void checkFlatpak();
    void runNext(Job &job);
    void finishPackages(const BoundedProcess::Result &result);
    void finishFlatpak(const BoundedProcess::Result &result);
    void completed(Job &job);
    void watch(Job &job, const QStringList &paths);
    QString findTool(const QString &name) const;
    QString path(const QString &absolute) const { return m_options.root + absolute; }
    qint64 now() const;
    std::optional<qint64> newestTime(const QStringList &paths, bool filesInside) const;
    QString checkupdatesDatabase() const;
    void setMetadata(aqtruncat::metrics::UpdatesMetrics &metrics, std::optional<qint64> time) const;
    QStringList flatpakInstallations() const;
    void unavailable(Job &job, const QString &manager, const QString &why);

    Options m_options;
    int m_intervalMinutes = 60;
    bool m_remote = false;
    bool m_active = false;
    Job m_packages;
    Job m_flatpak;
    QFileSystemWatcher m_watcher;
};
