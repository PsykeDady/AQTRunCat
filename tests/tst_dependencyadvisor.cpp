#include "dependencies/dependencyadvisor.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QtTest>

#include <algorithm>
#include <optional>

using namespace aqtruncat::dependencies;

namespace {

bool write(const QString &path, const QByteArray &data, QFile::Permissions permissions = QFile::ReadOwner | QFile::WriteOwner)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        return false;
    file.close();
    return file.setPermissions(permissions);
}

bool executableFile(const QString &path)
{
    return write(path, {}, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

QByteArray metric(const QString &title, const QJsonObject &source, const QJsonArray &dependencies = {},
                  std::optional<bool> enabled = std::nullopt)
{
    QJsonObject object{{QStringLiteral("id"), title.toLower()}, {QStringLiteral("title"), title},
                       {QStringLiteral("source"), source}};
    if (!dependencies.isEmpty())
        object.insert(QStringLiteral("dependencies"), dependencies);
    if (enabled)
        object.insert(QStringLiteral("enabled"), *enabled);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject dependency(const QString &executable, const QString &feature, const QString &package = {})
{
    QJsonObject result{{QStringLiteral("executable"), executable}, {QStringLiteral("feature"), feature}};
    if (!package.isEmpty())
        result.insert(QStringLiteral("packages"), QJsonObject{{QStringLiteral("arch"), package}});
    return result;
}

bool has(const QVector<DependencyFinding> &findings, const QString &id)
{
    return std::any_of(findings.cbegin(), findings.cend(), [&](const auto &finding) { return finding.id == id; });
}

AuditOptions isolated(const QString &root)
{
    AuditOptions options;
    options.path = root + QStringLiteral("/bin");
    options.dataDirectory = root + QStringLiteral("/AQTRunCat");
    options.sysRoot = root + QStringLiteral("/sys");
    options.root = root;
    options.distributionId = QStringLiteral("arch");
    options.fpsSource = QStringLiteral("off");
    options.showFps = false;
    options.updateIntervalMinutes = 0;
    options.plasmoidInstalled = false;
    options.plasmaSession = false;
    options.waylandSession = false;
    options.waylandPluginAvailable = true;
    return options;
}

} // namespace

class TestDependencyAdvisor : public QObject
{
    Q_OBJECT

private slots:
    void onlyReportsDependenciesForActiveFeatures()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        AuditOptions options = isolated(temporary.path());
        options.updateIntervalMinutes = 0;
        options.fpsSource = QStringLiteral("off");
        options.showFps = false;
        QCOMPARE(missing(options).size(), 0);
    }

    void detectsArchUpdateToolsOnlyWhenChecksAreEnabled()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        AuditOptions options = isolated(temporary.path());
        QVERIFY(executableFile(temporary.path() + QStringLiteral("/bin/pacman")));
        QVERIFY(QDir().mkpath(temporary.path() + QStringLiteral("/var/lib/pacman/local")));
        options.updateIntervalMinutes = 30;
        auto findings = missing(options);
        QVERIFY(has(findings, QStringLiteral("checkupdates")));
        QVERIFY(has(findings, QStringLiteral("flatpak")));
        QVERIFY(executableFile(temporary.path() + QStringLiteral("/bin/checkupdates")));
        QVERIFY(executableFile(temporary.path() + QStringLiteral("/bin/flatpak")));
        QVERIFY(!has(missing(options), QStringLiteral("checkupdates")));
        options.updateIntervalMinutes = 0;
        QVERIFY(!has(missing(options), QStringLiteral("flatpak")));
    }

    void recognizesCompatibleArchDerivative()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        AuditOptions options = isolated(temporary.path());
        options.distributionId = QStringLiteral("endeavouros");
        options.updateIntervalMinutes = 10;
        QVERIFY(executableFile(temporary.path() + QStringLiteral("/bin/pacman")));
        QVERIFY(QDir().mkpath(temporary.path() + QStringLiteral("/var/lib/pacman/local")));
        const auto findings = missing(options);
        QVERIFY(has(findings, QStringLiteral("checkupdates")));
        const auto it = std::find_if(findings.cbegin(), findings.cend(), [](const auto &item) {
            return item.id == QLatin1String("checkupdates");
        });
        QVERIFY(it != findings.cend());
        QCOMPARE(it->installCommand, QStringLiteral("sudo pacman -S pacman-contrib"));
    }

    void customDependenciesAreDeclarativeAndInheritedThroughGroups()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        AuditOptions options = isolated(temporary.path());
        const QString group = options.dataDirectory + QStringLiteral("/metrics/repository/metric.json");
        QVERIFY(write(group, metric(QStringLiteral("Tools"), {{QStringLiteral("type"), QStringLiteral("group")}},
            {dependency(QStringLiteral("jq"), QStringLiteral("JSON transform"), QStringLiteral("jq"))})));
        const QString child = options.dataDirectory + QStringLiteral("/metrics/repository/child/metric.json");
        QVERIFY(write(child, metric(QStringLiteral("Child metric"), {{QStringLiteral("type"), QStringLiteral("command")}})));
        auto findings = missing(options);
        QVERIFY(has(findings, QStringLiteral("jq")));
        QCOMPARE(std::count_if(findings.cbegin(), findings.cend(), [](const auto &finding) {
            return finding.id == QLatin1String("jq");
        }), 1);
        QVERIFY(executableFile(temporary.path() + QStringLiteral("/bin/jq")));
        QVERIFY(!has(missing(options), QStringLiteral("jq")));
        QVERIFY(write(group, metric(QStringLiteral("Tools"), {{QStringLiteral("type"), QStringLiteral("group")}},
            {dependency(QStringLiteral("jq"), QStringLiteral("JSON transform"))}, false)));
        QVERIFY(!has(missing(options), QStringLiteral("jq")));
    }

    void officialCliDependenciesAreFeatureScopedAndDoNotProbeAuthentication()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        AuditOptions options = isolated(temporary.path());
        const QString codex = options.dataDirectory + QStringLiteral("/metrics/codex/metric.json");
        const QString claude = options.dataDirectory + QStringLiteral("/metrics/claude-code/metric.json");
        QVERIFY(write(codex, metric(QStringLiteral("Codex"), {{QStringLiteral("type"), QStringLiteral("command")}},
                                    {dependency(QStringLiteral("codex"), QStringLiteral("Codex usage integration"))})));
        QVERIFY(write(claude, metric(QStringLiteral("Claude Code"), {{QStringLiteral("type"), QStringLiteral("command")}},
                                     {dependency(QStringLiteral("claude"), QStringLiteral("Claude Code usage integration"))})));
        auto findings = missing(options);
        QVERIFY(has(findings, QStringLiteral("codex")));
        QVERIFY(has(findings, QStringLiteral("claude")));
        QVERIFY(executableFile(temporary.path() + QStringLiteral("/bin/codex")));
        QVERIFY(executableFile(temporary.path() + QStringLiteral("/bin/claude")));
        QVERIFY(!has(missing(options), QStringLiteral("codex")));
        QVERIFY(!has(missing(options), QStringLiteral("claude")));
    }
};

QTEST_GUILESS_MAIN(TestDependencyAdvisor)
#include "tst_dependencyadvisor.moc"
