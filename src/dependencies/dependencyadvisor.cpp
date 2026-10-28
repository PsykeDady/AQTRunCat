#include "dependencyadvisor.h"

#include "metrics/systeminfo.h"
#include "metrics/packageupdates.h"

#include <QDir>
#include <QDirIterator>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QSettings>
#include <QStandardPaths>

#include <QSet>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

namespace aqtruncat::dependencies {
namespace {

QString executable(const QString &name, const QString &searchPath)
{
    return QStandardPaths::findExecutable(name, searchPath.split(QDir::listSeparator(), Qt::SkipEmptyParts));
}

struct PackageHint {
    QString package;
    QString command;
};

PackageHint hint(const QString &id, const QString &distro)
{
    if (id == QLatin1String("flatpak")) {
        if (distro == QLatin1String("arch") || distro == QLatin1String("manjaro")
            || distro == QLatin1String("endeavouros") || distro == QLatin1String("cachyos"))
            return {QStringLiteral("flatpak"), QStringLiteral("sudo pacman -S flatpak")};
        if (distro == QLatin1String("debian") || distro == QLatin1String("ubuntu")
            || distro == QLatin1String("linuxmint") || distro == QLatin1String("pop"))
            return {QStringLiteral("flatpak"), QStringLiteral("sudo apt install flatpak")};
        if (distro == QLatin1String("fedora"))
            return {QStringLiteral("flatpak"), QStringLiteral("sudo dnf install flatpak")};
    }
    if (id == QLatin1String("checkupdates"))
        return {QStringLiteral("pacman-contrib"), QStringLiteral("sudo pacman -S pacman-contrib")};
    if (id == QLatin1String("apt-get"))
        return {QStringLiteral("apt"), QStringLiteral("sudo apt install apt")};
    if (id == QLatin1String("dnf"))
        return {QStringLiteral("dnf"), QStringLiteral("sudo dnf install dnf")};
    if (id == QLatin1String("pacman"))
        return {QStringLiteral("pacman"), QStringLiteral("sudo pacman -S pacman")};
    if (id == QLatin1String("mangohud")) {
        if (distro == QLatin1String("arch") || distro == QLatin1String("manjaro")
            || distro == QLatin1String("endeavouros") || distro == QLatin1String("cachyos"))
            return {id, QStringLiteral("sudo pacman -S mangohud")};
        if (distro == QLatin1String("debian") || distro == QLatin1String("ubuntu"))
            return {id, QStringLiteral("sudo apt install mangohud")};
        if (distro == QLatin1String("fedora"))
            return {id, QStringLiteral("sudo dnf install mangohud")};
    }
    if (id == QLatin1String("hwdata")) {
        if (distro == QLatin1String("arch") || distro == QLatin1String("manjaro")
            || distro == QLatin1String("endeavouros") || distro == QLatin1String("cachyos"))
            return {id, QStringLiteral("sudo pacman -S hwdata")};
        if (distro == QLatin1String("fedora"))
            return {id, QStringLiteral("sudo dnf install hwdata")};
        if (distro == QLatin1String("debian") || distro == QLatin1String("ubuntu"))
            return {QStringLiteral("pci.ids"), QStringLiteral("sudo apt install pci.ids")};
    }
    if (id == QLatin1String("codex"))
        return {QStringLiteral("Codex CLI"), QStringLiteral("npm install -g @openai/codex@latest")};
    if (id == QLatin1String("claude"))
        return {QStringLiteral("Claude Code CLI"), QStringLiteral("npm install -g @anthropic-ai/claude-code")};
    if (id == QLatin1String("plasma-qml")) {
        if (distro == QLatin1String("arch"))
            return {QStringLiteral("Plasma 6 QML modules"), QStringLiteral("sudo pacman -S plasma-workspace libplasma kirigami kcmutils qt6-declarative")};
        if (distro == QLatin1String("fedora"))
            return {QStringLiteral("Plasma 6 QML modules"), QStringLiteral("sudo dnf install plasma-workspace libplasma kf6-kirigami kf6-kcmutils qt6-qtdeclarative")};
        if (distro == QLatin1String("debian") || distro == QLatin1String("ubuntu"))
            return {QStringLiteral("Plasma 6 QML modules"), QStringLiteral("sudo apt install plasma-workspace libplasma6 qml6-module-org-kde-kirigami qml6-module-org-kde-kcmutils qml6-module-qtquick-dialogs")};
    }
    if (id == QLatin1String("qt6-wayland")) {
        if (distro == QLatin1String("arch")) return {id, QStringLiteral("sudo pacman -S qt6-wayland")};
        if (distro == QLatin1String("fedora")) return {id, QStringLiteral("sudo dnf install qt6-qtwayland")};
        if (distro == QLatin1String("debian") || distro == QLatin1String("ubuntu"))
            return {id, QStringLiteral("sudo apt install qt6-wayland")};
    }
    return {id, QString()};
}

QString packageInstallCommand(const QString &package, const QString &distro)
{
    if (package.isEmpty()) return {};
    if (distro == QLatin1String("arch")) return QStringLiteral("sudo pacman -S %1").arg(package);
    if (distro == QLatin1String("debian")) return QStringLiteral("sudo apt install %1").arg(package);
    if (distro == QLatin1String("fedora")) return QStringLiteral("sudo dnf install %1").arg(package);
    return {};
}

void addIfMissing(QVector<DependencyFinding> &out, const QString &id, const QString &feature,
                  const QString &path, const QString &distro)
{
    bool present = !executable(id, path).isEmpty();
    if (id == QLatin1String("mangohud")) {
        const QStringList layerDirs{QStringLiteral("/usr/share/vulkan/implicit_layer.d"),
                                    QStringLiteral("/usr/local/share/vulkan/implicit_layer.d"),
                                    QStringLiteral("/etc/vulkan/implicit_layer.d"),
                                    QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                                        + QStringLiteral("/vulkan/implicit_layer.d")};
        for (const QString &directory : layerDirs) {
            QDirIterator layers(directory, QDir::Files);
            while (layers.hasNext()) {
                const QString filename = QFileInfo(layers.next()).fileName().toLower();
                if (filename.startsWith(QStringLiteral("mangohud")) && filename.endsWith(QStringLiteral(".json"))) {
                    present = true;
                    break;
                }
            }
            if (present)
                break;
        }
    }
    if (present)
        return;
    const PackageHint install = hint(id, distro);
    out.append(DependencyFinding{id, feature, id, install.package, install.command});
}

bool jsonBool(const QJsonObject &object, const QString &key, bool fallback = true)
{
    const QJsonValue value = object.value(key);
    return value.isBool() ? value.toBool() : fallback;
}

QStringList qmlImports()
{
    return {QStringLiteral("org/kde/plasma/plasmoid"), QStringLiteral("org/kde/plasma/core"),
            QStringLiteral("org/kde/plasma/components"), QStringLiteral("org/kde/plasma/extras"),
            QStringLiteral("org/kde/plasma/workspace/dbus"), QStringLiteral("org/kde/kirigami"),
            QStringLiteral("org/kde/kcmutils"), QStringLiteral("QtQuick/Dialogs")};
}

bool hasWaylandPlatformPlugin()
{
    QStringList paths = QCoreApplication::libraryPaths();
    paths.append(QLibraryInfo::path(QLibraryInfo::PluginsPath));
    for (const QString &base : std::as_const(paths)) {
        const QDir platforms(QDir(base).filePath(QStringLiteral("platforms")));
        const QStringList plugins = platforms.entryList({QStringLiteral("libqwayland*.so*")}, QDir::Files);
        if (!plugins.isEmpty())
            return true;
    }
    return false;
}

bool libraryFileExists(const QString &name)
{
    const QStringList roots{QStringLiteral("/lib"), QStringLiteral("/usr/lib"), QStringLiteral("/usr/local/lib"),
                            QStringLiteral("/lib64"), QStringLiteral("/usr/lib64")};
    for (const QString &root : roots) {
        const QDir base(root);
        if (!base.entryList({name + QStringLiteral("*")}, QDir::Files).isEmpty())
            return true;
        for (const QString &subdir : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (!QDir(base.filePath(subdir)).entryList({name + QStringLiteral("*")}, QDir::Files).isEmpty())
                return true;
        }
    }
    return false;
}

void scanMetricFile(const QString &filePath, const QString &path, const QString &distro,
                    QVector<DependencyFinding> &out, QSet<QString> &visited,
                    bool inheritedEnabled = true, QJsonArray inheritedDependencies = {}, int depth = 0)
{
    if (depth > 4)
        return;
    QFile file(filePath);
    const QString canonical = QFileInfo(filePath).canonicalFilePath();
    if (canonical.isEmpty() || visited.contains(canonical))
        return;
    visited.insert(canonical);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024)
        return;
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    if (object.isEmpty())
        return;
    const bool enabled = inheritedEnabled && jsonBool(object, QStringLiteral("enabled"));
    const QJsonValue deps = object.value(QStringLiteral("dependencies"));
    const QJsonArray dependencies = deps.isArray() ? deps.toArray() : inheritedDependencies;
    if (enabled) {
        for (const QJsonValue &entry : dependencies) {
            const QJsonObject dependency = entry.toObject();
            const QString name = dependency.value(QStringLiteral("executable")).toString().trimmed();
            if (name.isEmpty() || name.contains(u'/'))
                continue;
            if (!executable(name, path).isEmpty())
                continue;
            const QString feature = dependency.value(QStringLiteral("feature")).toString(
                object.value(QStringLiteral("title")).toString(QStringLiteral("Custom Metric")));
            const QJsonObject packages = dependency.value(QStringLiteral("packages")).toObject();
            QString package = packages.value(distro).toString(name);
            static const QRegularExpression safePackage(QStringLiteral("^[A-Za-z0-9.+_-]{1,128}$"));
            if (!safePackage.match(package).hasMatch())
                package = name;
            const QString install = (name == QLatin1String("codex") || name == QLatin1String("claude"))
                ? hint(name, distro).command : packageInstallCommand(package, distro);
            out.append(DependencyFinding{name, feature, name, package, install});
        }
    }
    const QJsonObject source = object.value(QStringLiteral("source")).toObject();
    if (source.value(QStringLiteral("type")) != QLatin1String("group") || !enabled)
        return;
    const QDir directory(QFileInfo(filePath).absolutePath());
    const QStringList children = directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &child : children) {
        const QString childFile = directory.filePath(child + QStringLiteral("/metric.json"));
        if (QFileInfo(childFile).isFile())
            scanMetricFile(childFile, path, distro, out, visited, enabled, dependencies, depth + 1);
    }
}

} // namespace

AuditOptions currentUserOptions()
{
    AuditOptions options;
    options.path = qEnvironmentVariable("PATH");
    const QString userBin = QDir::home().filePath(QStringLiteral(".local/bin"));
    if (!options.path.split(QDir::listSeparator()).contains(userBin))
        options.path += (options.path.isEmpty() ? QString() : QString(QDir::listSeparator())) + userBin;
    options.dataDirectory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/AQTRunCat");
    const QString currentDesktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    options.plasmaSession = qEnvironmentVariable("KDE_SESSION_VERSION") == QLatin1String("6")
        || currentDesktop.contains(QStringLiteral("kde")) || qEnvironmentVariable("KDE_FULL_SESSION") == QLatin1String("true");
    options.waylandSession = !qEnvironmentVariable("WAYLAND_DISPLAY").isEmpty();
    options.plasmoidInstalled = !QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
        QStringLiteral("plasma/plasmoids/io.github.psykedady.aqtruncat/metadata.json")).isEmpty();
    if (options.plasmaSession && options.plasmoidInstalled) {
        const QStringList importPaths = QLibraryInfo::paths(QLibraryInfo::QmlImportsPath);
        for (const QString &module : qmlImports()) {
            bool found = false;
            for (const QString &base : importPaths) {
                if (QFileInfo::exists(QDir(base).filePath(module + QStringLiteral("/qmldir")))) {
                    found = true;
                    break;
                }
            }
            if (!found)
                options.missingPlasmaModules.append(module);
        }
    }
    options.waylandPluginAvailable = hasWaylandPlatformPlugin();
    options.nvmlAvailable = libraryFileExists(QStringLiteral("libnvidia-ml.so"));
    options.gpuModelDatabaseAvailable = QFileInfo::exists(QStringLiteral("/usr/share/hwdata/pci.ids"))
        || QFileInfo::exists(QStringLiteral("/usr/share/misc/pci.ids"))
        || QFileInfo::exists(QStringLiteral("/usr/share/pci.ids"));
    QDirIterator pciDevices(QStringLiteral("/sys/bus/pci/devices"), {QStringLiteral("vendor")}, QDir::Files);
    while (pciDevices.hasNext()) {
        QFile vendor(pciDevices.next());
        if (vendor.open(QIODevice::ReadOnly) && vendor.readAll().trimmed() == "0x10de") {
            options.nvidiaGpuPresent = true;
            break;
        }
    }
    const auto distribution = metrics::readDistribution("/");
    if (distribution.info) {
        options.distributionId = QString::fromStdString(distribution.info->id);
        for (const std::string &like : distribution.info->idLike)
            options.distributionLike.append(QString::fromStdString(like));
    }
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("AQTRunCat"), QStringLiteral("AQTRunCat"));
    options.fpsSource = settings.value(QStringLiteral("fps/source"), QStringLiteral("mangohud")).toString();
    options.updateIntervalMinutes = settings.value(QStringLiteral("updates/intervalMinutes"), 60).toInt();
    options.showFps = settings.value(QStringLiteral("display/fps"), true).toBool();
    options.customMetricFiles = settings.value(QStringLiteral("customMetrics/files")).toStringList();
    return options;
}

QVector<DependencyFinding> missing(const AuditOptions &options)
{
    QVector<DependencyFinding> out;
    const auto familyOf = [](const QString &id) {
        if (id == QLatin1String("arch") || id == QLatin1String("archarm") || id == QLatin1String("manjaro")
            || id == QLatin1String("endeavouros") || id == QLatin1String("cachyos")) return QStringLiteral("arch");
        if (id == QLatin1String("debian") || id == QLatin1String("ubuntu") || id == QLatin1String("linuxmint")
            || id == QLatin1String("pop") || id == QLatin1String("raspbian")) return QStringLiteral("debian");
        if (id == QLatin1String("fedora") || id == QLatin1String("rhel") || id == QLatin1String("centos")
            || id == QLatin1String("rocky") || id == QLatin1String("almalinux")) return QStringLiteral("fedora");
        return QString();
    };
    QString distro = familyOf(options.distributionId);
    if (distro.isEmpty()) {
        for (const QString &like : options.distributionLike) {
            distro = familyOf(like);
            if (!distro.isEmpty()) break;
        }
    }
    if (options.updateIntervalMinutes > 0) {
        metrics::PackageManagerAvailability available;
        available.pacman = !executable(QStringLiteral("pacman"), options.path).isEmpty()
            && QFileInfo::exists(options.root + QStringLiteral("/var/lib/pacman/local"));
        available.apt = !executable(QStringLiteral("apt-get"), options.path).isEmpty()
            && QFileInfo::exists(options.root + QStringLiteral("/var/lib/dpkg/status"));
        available.dnf = !executable(QStringLiteral("dnf"), options.path).isEmpty()
            && (QFileInfo::exists(options.root + QStringLiteral("/var/lib/rpm"))
                || QFileInfo::exists(options.root + QStringLiteral("/usr/lib/sysimage/rpm")));
        const metrics::PackageManager manager = metrics::choosePackageManager(available,
            options.distributionId.toStdString(), [&] {
                std::vector<std::string> result;
                for (const QString &like : options.distributionLike) result.push_back(like.toStdString());
                return result;
            }());
        if (manager == metrics::PackageManager::Pacman) {
            addIfMissing(out, QStringLiteral("checkupdates"), QStringLiteral("Official Arch repository updates"),
                         options.path, distro);
        } else if (manager == metrics::PackageManager::Apt) {
            addIfMissing(out, QStringLiteral("apt-get"), QStringLiteral("Distribution package update metric"), options.path, distro);
        } else if (manager == metrics::PackageManager::Dnf) {
            addIfMissing(out, QStringLiteral("dnf"), QStringLiteral("Distribution package update metric"), options.path, distro);
        } else {
            const QString expected = distro == QLatin1String("arch") ? QStringLiteral("pacman")
                : distro == QLatin1String("debian") ? QStringLiteral("apt-get")
                : distro == QLatin1String("fedora") ? QStringLiteral("dnf") : QString();
            if (!expected.isEmpty())
                addIfMissing(out, expected, QStringLiteral("Distribution package update metric"), options.path, distro);
        }
        addIfMissing(out, QStringLiteral("flatpak"), QStringLiteral("Flatpak updates metric"), options.path, distro);
    }
    if (options.showFps && options.fpsSource == QLatin1String("mangohud"))
        addIfMissing(out, QStringLiteral("mangohud"), QStringLiteral("FPS metric (MangoHud log)"), options.path, distro);
    if (options.waylandSession && !options.waylandPluginAvailable)
        out.append(DependencyFinding{QStringLiteral("qt6-wayland"), QStringLiteral("Native Qt windows in this Wayland session"),
                                     QStringLiteral("qt6-wayland"), QStringLiteral("qt6-wayland"),
                                     hint(QStringLiteral("qt6-wayland"), distro).command});
    if (options.plasmaSession && options.plasmoidInstalled && !options.missingPlasmaModules.isEmpty()) {
        const PackageHint plasmaHint = hint(QStringLiteral("plasma-qml"), distro);
        const QString plasmaFeature = QStringLiteral("Installed Plasma widget; missing QML modules: ")
            + options.missingPlasmaModules.join(QStringLiteral(", "));
        out.append(DependencyFinding{QStringLiteral("plasma-qml"), plasmaFeature, QStringLiteral("QML modules"),
                                     plasmaHint.package, plasmaHint.command});
    }

    const QString metricRoot = options.dataDirectory + QStringLiteral("/metrics");
    QStringList metricFiles;
    QDirIterator it(metricRoot, {QStringLiteral("metric.json")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        metricFiles.append(it.next());
    std::sort(metricFiles.begin(), metricFiles.end(), [](const QString &a, const QString &b) {
        return a.count(u'/') < b.count(u'/');
    });
    QSet<QString> visited;
    for (const QString &file : std::as_const(metricFiles)) {
        const QString canonical = QFileInfo(file).canonicalFilePath();
        if (canonical.isEmpty() || visited.contains(canonical))
            continue;
        scanMetricFile(file, options.path, distro, out, visited);
    }
    for (const QString &file : options.customMetricFiles)
        scanMetricFile(file, options.path, distro, out, visited);

    // Optional model-name database used by the GPU collector. Without it,
    // GPU metrics still work and AQTRunCat falls back to PCI IDs.
    bool hasPciGpu = false;
    QDirIterator pci(options.sysRoot + QStringLiteral("/bus/pci/devices"), {QStringLiteral("class")}, QDir::Files);
    while (pci.hasNext()) {
        QFile classFile(pci.next());
        if (!classFile.open(QIODevice::ReadOnly))
            continue;
        if (classFile.readAll().trimmed().startsWith("0x03")) {
            hasPciGpu = true;
            break;
        }
    }
    if (hasPciGpu && !options.gpuModelDatabaseAvailable) {
        const PackageHint install = hint(QStringLiteral("hwdata"), distro);
        out.append(DependencyFinding{QStringLiteral("pci.ids"), QStringLiteral("Human-readable GPU model names"),
                                     install.package, install.package, install.command});
    }
    bool nvidiaGpu = options.nvidiaGpuPresent;
    if (!nvidiaGpu) {
        QDirIterator devices(options.sysRoot + QStringLiteral("/bus/pci/devices"), {QStringLiteral("vendor")}, QDir::Files);
        while (devices.hasNext()) {
            QFile vendor(devices.next());
            if (vendor.open(QIODevice::ReadOnly) && vendor.readAll().trimmed() == "0x10de") {
                nvidiaGpu = true;
                break;
            }
        }
    }
    if (nvidiaGpu && !options.nvmlAvailable) {
        out.append(DependencyFinding{QStringLiteral("libnvidia-ml.so.1"),
                                     QStringLiteral("Optional NVIDIA GPU telemetry enhancement"),
                                     QStringLiteral("NVIDIA driver library"), QString(), QString()});
    }
    QVector<DependencyFinding> unique;
    QSet<QString> ids;
    for (const DependencyFinding &finding : std::as_const(out)) {
        const QString key = finding.id + u'\n' + finding.feature;
        if (ids.contains(key))
            continue;
        ids.insert(key);
        unique.append(finding);
    }
    return unique;
}

QString formatReport(const AuditOptions &options)
{
    const auto findings = missing(options);
    if (findings.isEmpty())
        return QStringLiteral("No missing dependencies for the enabled AQTRunCat features.");
    QStringList lines{QStringLiteral("Missing dependencies for enabled AQTRunCat features:")};
    for (const auto &finding : findings) {
        lines << QStringLiteral("- %1 — %2").arg(finding.id, finding.feature);
        if (!finding.installCommand.isEmpty())
            lines << QStringLiteral("  Install: %1").arg(finding.installCommand);
        else if (!finding.package.isEmpty())
            lines << QStringLiteral("  Package: %1 (install using your distribution's package manager)").arg(finding.package);
    }
    lines << QStringLiteral("No packages were installed.");
    return lines.join(u'\n');
}

} // namespace aqtruncat::dependencies
