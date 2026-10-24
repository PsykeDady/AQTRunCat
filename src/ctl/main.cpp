// aqtruncatctl: reference command-line client of the AQTRunCat backend API
// (F4.1). It talks to aqtruncatd over D-Bus only — it never reads /proc or
// any provider file itself. Contract: docs/backend-dbus-api-cli.md.

#include "api/apischema.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include <csignal>
#include <cstdio>
#include <optional>
#include <utility>
#include <sys/socket.h>
#include <unistd.h>

namespace {

enum Exit {
    Ok = 0,
    Failed = 1,      // the backend reported a failure
    Usage = 2,       // invalid command line
    Unavailable = 3, // no backend (not running and could not be started)
    Rejected = 4,    // the backend rejected the request (invalid value, unknown id)
};

int signalPipe[2] = {-1, -1};
void onUnixSignal(int)
{
    const char c = 1;
    [[maybe_unused]] const auto n = ::write(signalPipe[1], &c, 1);
}

void out(const QString &text) { std::fputs(qPrintable(text + u'\n'), stdout); }
void err(const QString &text) { std::fputs(qPrintable(QStringLiteral("aqtruncatctl: ") + text + u'\n'), stderr); }

const char Help[] =
    "Usage: aqtruncatctl [--json] [--no-start] COMMAND\n"
    "\n"
    "Reference client of the AQTRunCat backend (aqtruncatd) over D-Bus.\n"
    "\n"
    "Commands:\n"
    "  status                         backend, runner and integrations summary\n"
    "  metrics [ID...]                all metrics, or the given ids (cpu, uptime,\n"
    "                                 gpu:<device>, fps, updates.packages, kernel…)\n"
    "          [--category builtin|integration|custom]\n"
    "  watch                          print metric changes as they happen (Ctrl+C)\n"
    "  integrations                   official integrations (Claude Code, Codex)\n"
    "  integration enable|disable ID\n"
    "  integration live-updates ID on|off\n"
    "  settings                       the shared settings\n"
    "  settings set KEY VALUE         e.g. speedMode slow-down, autostart false,\n"
    "                                 metricsBarItems '[\"cpu\",\"memory\"]',\n"
    "                                 gpuDevice auto, fpsSource off,\n"
    "                                 updatesIntervalMinutes 60 (0 = off),\n"
    "                                 updatesRemoteRefresh false\n"
    "  custom add|remove PATH         (un)register a Custom Metrics file\n"
    "  custom reload                  rescan the Custom Metrics files and apply\n"
    "                                 what was added, changed or removed\n"
    "\n"
    "Options:\n"
    "  --json      machine-readable output (the API's JSON, one document; for\n"
    "              watch, one MetricsChanged object per line)\n"
    "  --no-start  do not start the backend when it is not running\n"
    "  -h, --help     show this help\n"
    "  -v, --version  show the version\n"
    "\n"
    "Exit status: 0 success, 1 the backend reported a failure, 2 invalid command\n"
    "line, 3 backend unavailable, 4 request rejected (invalid value, unknown id).\n";

class Client
{
public:
    explicit Client(bool mayStart) : m_bus(QDBusConnection::sessionBus()), m_mayStart(mayStart) {}

    // A method call; on success the reply's first string (JSON) argument.
    std::optional<QString> call(const QString &method, const QVariantList &arguments, int &status, int timeoutMs = 15000)
    {
        if (!m_bus.isConnected()) {
            err(QStringLiteral("no D-Bus session bus"));
            status = Unavailable;
            return std::nullopt;
        }
        for (int attempt = 0; attempt < 2; ++attempt) {
            QDBusMessage message = QDBusMessage::createMethodCall(aqtruncat::api::ServiceName, aqtruncat::api::ObjectPath,
                                                                  aqtruncat::api::InterfaceName, method);
            message.setArguments(arguments);
            message.setAutoStartService(m_mayStart);
            const QDBusMessage reply = m_bus.call(message, QDBus::Block, timeoutMs);
            if (reply.type() != QDBusMessage::ErrorMessage) {
                status = Ok;
                return reply.arguments().isEmpty() ? QString() : reply.arguments().first().toString();
            }
            const QString name = reply.errorName();
            const bool absent = name == QLatin1String("org.freedesktop.DBus.Error.ServiceUnknown")
                || name == QLatin1String("org.freedesktop.DBus.Error.NameHasNoOwner")
                || name == QLatin1String("org.freedesktop.DBus.Error.Spawn.ServiceNotFound");
            if (absent && attempt == 0 && m_mayStart && startBackend())
                continue;
            if (absent) {
                err(m_mayStart ? QStringLiteral("the AQTRunCat backend is not running and could not be started")
                               : QStringLiteral("the AQTRunCat backend is not running"));
                status = Unavailable;
            } else if (name == aqtruncat::api::ErrorInvalidArgument || name == aqtruncat::api::ErrorNotFound) {
                err(reply.errorMessage());
                status = Rejected;
            } else if (name == QLatin1String("org.freedesktop.DBus.Error.NoReply")
                       || name == QLatin1String("org.freedesktop.DBus.Error.Timeout")) {
                err(QStringLiteral("the backend did not answer in time"));
                status = Unavailable;
            } else {
                err(reply.errorMessage());
                status = Failed;
            }
            return std::nullopt;
        }
        status = Unavailable;
        return std::nullopt;
    }

    QDBusConnection &bus() { return m_bus; }

private:
    // No activation file (e.g. a build tree): start the backend installed
    // next to this program, then wait for its name (at most 5 s).
    bool startBackend()
    {
        const QFileInfo backend(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("aqtruncatd")));
        if (!backend.isFile() || !backend.isExecutable() || !QProcess::startDetached(backend.absoluteFilePath(), {}))
            return false;
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 5000) {
            if (m_bus.interface()->isServiceRegistered(aqtruncat::api::ServiceName))
                return true;
            QThread::msleep(50);
        }
        return false;
    }

    QDBusConnection m_bus;
    bool m_mayStart;
};

QJsonObject object(const QString &text) { return QJsonDocument::fromJson(text.toUtf8()).object(); }
QJsonArray array(const QString &text) { return QJsonDocument::fromJson(text.toUtf8()).array(); }

QString pretty(const QString &json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    return QString::fromUtf8(document.toJson(QJsonDocument::Indented)).trimmed();
}

QString padded(const QString &text, int width)
{
    return text.size() >= width ? text + u' ' : text.leftJustified(width);
}

QString stateText(const QJsonObject &metric)
{
    const QString state = metric.value(QStringLiteral("state")).toString();
    if (state == QLatin1String("ok"))
        return {};
    const QString message = metric.value(QStringLiteral("message")).toString();
    return message.isEmpty() ? state : state + QStringLiteral(": ") + message;
}

void printMetric(const QJsonObject &metric)
{
    QString value = metric.value(QStringLiteral("formattedValue")).toString();
    const QString state = stateText(metric);
    // v2F1.3: a failing source's last values are not current: only the error.
    const bool failed = metric.value(QStringLiteral("state")) == QLatin1String("error");
    const QJsonArray items = failed ? QJsonArray() : metric.value(QStringLiteral("items")).toArray();
    if (!items.isEmpty() || failed)
        value.clear(); // multi-value: the items speak
    if (!state.isEmpty())
        value = value.isEmpty() ? QStringLiteral("(%1)").arg(state) : value + QStringLiteral("  (%1)").arg(state);
    const QString detail = metric.value(QStringLiteral("detail")).toString();
    // Custom and integration ids carry a file path: shown with --json only.
    const QString id = metric.value(QStringLiteral("category")) == QLatin1String("builtin")
        ? metric.value(QStringLiteral("id")).toString() : QString();
    out(QStringLiteral("  %1%2%3%4").arg(padded(id, 18),
                                         padded(metric.value(QStringLiteral("title")).toString(), 22), value,
                                         detail.isEmpty() ? QString() : QStringLiteral("  ") + detail));
    // v2F1.6: what does not stop a Custom Metric (a missing icon, a value
    // outside its range).
    for (const QJsonValue &warning : metric.value(QStringLiteral("warnings")).toArray())
        out(QStringLiteral("      ! %1").arg(warning.toString()));
    for (const QJsonValue &itemValue : items) {
        const QJsonObject item = itemValue.toObject();
        const QString itemDetail = item.value(QStringLiteral("detail")).toString();
        out(QStringLiteral("      %1%2%3%4").arg(item.value(QStringLiteral("warning")).toBool() ? QStringLiteral("! ") : QStringLiteral("  "),
                                               padded(item.value(QStringLiteral("title")).toString(), 28),
                                               item.value(QStringLiteral("formattedValue")).toString(),
                                               itemDetail.isEmpty() ? QString() : QStringLiteral("  ") + itemDetail));
    }
}

int metrics(Client &client, bool json, const QStringList &ids, const QString &category)
{
    int status = Ok;
    const std::optional<QString> reply = client.call(QStringLiteral("GetMetrics"), {}, status);
    if (!reply)
        return status;
    QJsonArray selected;
    QStringList missing = ids;
    for (const QJsonValue &value : object(*reply).value(QStringLiteral("metrics")).toArray()) {
        const QJsonObject metric = value.toObject();
        if (!category.isEmpty() && metric.value(QStringLiteral("category")) != category)
            continue;
        const QString id = metric.value(QStringLiteral("id")).toString();
        if (!ids.isEmpty() && !ids.contains(id))
            continue;
        missing.removeAll(id);
        selected.append(metric);
    }
    if (!missing.isEmpty()) {
        err(QStringLiteral("no such metric: %1").arg(missing.join(QStringLiteral(", "))));
        return Rejected;
    }
    if (json) {
        out(QString::fromUtf8(QJsonDocument(selected).toJson(QJsonDocument::Indented)).trimmed());
        return Ok;
    }
    const QList<QPair<QString, QString>> sections{{QStringLiteral("builtin"), QStringLiteral("Built-in metrics")},
                                                  {QStringLiteral("integration"), QStringLiteral("Integrations")},
                                                  {QStringLiteral("custom"), QStringLiteral("Custom Metrics")}};
    bool first = true;
    for (const auto &[name, heading] : sections) {
        bool any = false;
        for (const QJsonValue &value : std::as_const(selected)) {
            if (value.toObject().value(QStringLiteral("category")) != name)
                continue;
            if (!any) {
                if (!first)
                    out(QString());
                out(heading);
                any = true;
                first = false;
            }
            printMetric(value.toObject());
        }
    }
    return Ok;
}

QString integrationLine(const QJsonObject &i)
{
    QString text = i.value(QStringLiteral("title")).toString() + QStringLiteral(": ");
    if (!i.value(QStringLiteral("adapterInstalled")).toBool())
        return text + QStringLiteral("adapter not installed");
    if (i.value(QStringLiteral("userPackage")).toBool())
        return text + QStringLiteral("a user-defined Custom Metric uses its id");
    text += i.value(QStringLiteral("enabled")).toBool() ? QStringLiteral("on") : QStringLiteral("off");
    if (i.value(QStringLiteral("enabled")).toBool())
        text += QStringLiteral(", live updates %1").arg(i.value(QStringLiteral("liveUpdates")).toString());
    if (!i.value(QStringLiteral("providerFound")).toBool())
        text += QStringLiteral(" (not found on this system)");
    return text;
}

int status(Client &client, bool json)
{
    int code = Ok;
    const std::optional<QString> state = client.call(QStringLiteral("GetState"), {}, code);
    if (!state)
        return code;
    const QJsonObject s = object(*state);
    if (json) {
        QJsonObject summary{{QStringLiteral("info"), s.value(QStringLiteral("info"))},
                            {QStringLiteral("runner"), s.value(QStringLiteral("runner"))},
                            {QStringLiteral("integrations"), s.value(QStringLiteral("integrations"))},
                            {QStringLiteral("generation"), s.value(QStringLiteral("generation"))}};
        out(QString::fromUtf8(QJsonDocument(summary).toJson(QJsonDocument::Indented)).trimmed());
        return Ok;
    }
    const QJsonObject info = s.value(QStringLiteral("info")).toObject();
    const QJsonObject runner = s.value(QStringLiteral("runner")).toObject();
    int counts[3] = {0, 0, 0};
    for (const QJsonValue &value : s.value(QStringLiteral("metrics")).toArray()) {
        const QString category = value.toObject().value(QStringLiteral("category")).toString();
        ++counts[category == QLatin1String("builtin") ? 0 : category == QLatin1String("integration") ? 1 : 2];
    }
    const QDateTime started = QDateTime::fromString(info.value(QStringLiteral("startedAt")).toString(), Qt::ISODate);
    out(QStringLiteral("Backend:       running (aqtruncatd %1, API %2, schema %3, since %4)")
            .arg(info.value(QStringLiteral("version")).toString())
            .arg(info.value(QStringLiteral("api")).toInt())
            .arg(info.value(QStringLiteral("schema")).toInt())
            .arg(started.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
    out(QStringLiteral("Runner:        %1 · %2 · %3 fps")
            .arg(runner.value(QStringLiteral("runnerId")).toString(), runner.value(QStringLiteral("speedMode")).toString())
            .arg(runner.value(QStringLiteral("fps")).toInt()));
    out(QStringLiteral("Metrics:       %1 built-in · %2 integrations · %3 custom").arg(counts[0]).arg(counts[1]).arg(counts[2]));
    QStringList integrations;
    for (const QJsonValue &value : s.value(QStringLiteral("integrations")).toArray())
        integrations << integrationLine(value.toObject());
    out(QStringLiteral("Integrations:  %1").arg(integrations.join(QStringLiteral(" · "))));
    return Ok;
}

int integrations(Client &client, bool json)
{
    int code = Ok;
    const std::optional<QString> reply = client.call(QStringLiteral("GetIntegrations"), {}, code);
    if (!reply)
        return code;
    if (json) {
        out(pretty(*reply));
        return Ok;
    }
    for (const QJsonValue &value : array(*reply)) {
        const QJsonObject i = value.toObject();
        out(QStringLiteral("%1  %2").arg(padded(i.value(QStringLiteral("id")).toString(), 12), integrationLine(i)));
    }
    return Ok;
}

int settings(Client &client, bool json)
{
    int code = Ok;
    const std::optional<QString> reply = client.call(QStringLiteral("GetSettings"), {}, code);
    if (!reply)
        return code;
    if (json) {
        out(pretty(*reply));
        return Ok;
    }
    const QJsonObject s = object(*reply);
    for (auto it = s.constBegin(); it != s.constEnd(); ++it) {
        QString value;
        if (it->isArray()) {
            QStringList entries;
            for (const QJsonValue &entry : it->toArray())
                entries << entry.toString();
            value = entries.isEmpty() ? QStringLiteral("(none)") : entries.join(QStringLiteral(", "));
        } else if (it->isBool()) {
            value = it->toBool() ? QStringLiteral("true") : QStringLiteral("false");
        } else if (it->isDouble()) {
            value = QString::number(it->toInt());
        } else {
            value = it->toString();
        }
        out(QStringLiteral("%1= %2").arg(padded(it.key(), 24), value));
    }
    return Ok;
}

int simple(Client &client, const QString &method, const QVariantList &arguments, const QString &done, int timeoutMs = 15000)
{
    int code = Ok;
    const std::optional<QString> reply = client.call(method, arguments, code, timeoutMs);
    if (!reply)
        return code;
    out(reply->isEmpty() ? done : reply->trimmed());
    return Ok;
}

// v2F1.6: ReloadCustomMetrics; a refused reload is the backend's error
// (exit status 1), configuration errors in an applied one are listed.
int reloadCustomMetrics(Client &client, bool json)
{
    int code = Ok;
    const std::optional<QString> reply = client.call(QStringLiteral("ReloadCustomMetrics"), {}, code, 60000);
    if (!reply)
        return code;
    if (json) {
        out(reply->trimmed());
        return Ok;
    }
    const aqtruncat::custommetrics::ReloadReport report = aqtruncat::api::reloadReportFromJson(object(*reply));
    // The errors and warnings one per line below, not folded into the summary.
    aqtruncat::custommetrics::ReloadReport counts = report;
    counts.errors.clear();
    counts.warnings.clear();
    QString text = aqtruncat::custommetrics::reloadSummary(counts);
    for (const auto &[label, entries] : {std::pair{QStringLiteral("added"), &report.added},
                                         std::pair{QStringLiteral("changed"), &report.changed},
                                         std::pair{QStringLiteral("removed"), &report.removed}}) {
        for (const aqtruncat::custommetrics::ReloadEntry &entry : *entries)
            text += QStringLiteral("\n  %1: %2").arg(label, entry.name);
    }
    for (const aqtruncat::custommetrics::ReloadEntry &entry : report.errors)
        text += QStringLiteral("\n  error: %1: %2").arg(entry.name, entry.message);
    for (const aqtruncat::custommetrics::ReloadEntry &entry : report.warnings)
        text += QStringLiteral("\n  warning: %1: %2").arg(entry.name, entry.message);
    out(text);
    return Ok;
}

// Prints MetricsChanged signals (see watch).
class ChangePrinter : public QObject
{
    Q_OBJECT

public:
    bool json = false;
    quint64 generation = 0;

public slots:
    void changed(qulonglong next, const QString &changes)
    {
        if (json) {
            out(changes);
        } else {
            if (generation && next != generation + 1)
                out(QStringLiteral("(missed %1 update(s))").arg(next - generation - 1));
            QStringList parts;
            for (const QJsonValue &value : object(changes).value(QStringLiteral("metrics")).toArray()) {
                const QJsonObject metric = value.toObject();
                QString shown = metric.value(QStringLiteral("compactValue")).toString();
                if (shown.isEmpty())
                    shown = metric.value(QStringLiteral("state")).toString();
                parts << QStringLiteral("%1 %2").arg(metric.value(QStringLiteral("id")).toString(), shown);
            }
            out(QStringLiteral("%1  %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")),
                                            parts.join(QStringLiteral(" · "))));
        }
        std::fflush(stdout);
        generation = next;
    }
};

int watch(QCoreApplication &app, Client &client, bool json)
{
    int code = Ok;
    const std::optional<QString> state = client.call(QStringLiteral("GetMetrics"), {}, code);
    if (!state)
        return code;
    ChangePrinter printer;
    printer.json = json;
    printer.generation = quint64(object(*state).value(QStringLiteral("generation")).toDouble());
    if (!client.bus().connect(aqtruncat::api::ServiceName, aqtruncat::api::ObjectPath, aqtruncat::api::InterfaceName,
                              QStringLiteral("MetricsChanged"), &printer, SLOT(changed(qulonglong,QString)))) {
        err(QStringLiteral("cannot subscribe to MetricsChanged"));
        return Failed;
    }
    ::socketpair(AF_UNIX, SOCK_STREAM, 0, signalPipe);
    QSocketNotifier notifier(signalPipe[0], QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &app, &QCoreApplication::quit);
    std::signal(SIGINT, onUnixSignal);
    std::signal(SIGTERM, onUnixSignal);
    return app.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    bool json = false;
    bool mayStart = true;
    QString category;
    QStringList rest;
    for (int i = 0; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg == QLatin1String("-h") || arg == QLatin1String("--help")) {
            std::fputs(Help, stdout);
            return Ok;
        }
        if (arg == QLatin1String("-v") || arg == QLatin1String("--version")) {
            std::printf("aqtruncatctl %s (API %d)\n", AQTRUNCAT_VERSION, aqtruncat::api::ApiVersion);
            return Ok;
        }
        if (arg == QLatin1String("--json"))
            json = true;
        else if (arg == QLatin1String("--no-start"))
            mayStart = false;
        else if (arg == QLatin1String("--category") && i + 1 < args.size())
            category = args.at(++i);
        else if (arg.startsWith(QLatin1String("--")))
            return err(QStringLiteral("unknown option \"%1\"; see --help").arg(arg)), Usage;
        else
            rest << arg;
    }
    if (rest.isEmpty()) {
        std::fputs(Help, stderr);
        return Usage;
    }
    if (!category.isEmpty() && category != QLatin1String("builtin") && category != QLatin1String("integration")
        && category != QLatin1String("custom"))
        return err(QStringLiteral("--category must be builtin, integration or custom")), Usage;
    const QString command = rest.takeFirst();
    Client client(mayStart);
    auto usage = [](const QString &text) { return err(text + QStringLiteral("; see --help")), int(Usage); };
    if (!category.isEmpty() && command != QLatin1String("metrics"))
        return usage(QStringLiteral("--category only applies to metrics"));

    if (command == QLatin1String("status") && rest.isEmpty())
        return status(client, json);
    if (command == QLatin1String("metrics"))
        return metrics(client, json, rest, category);
    if (command == QLatin1String("watch") && rest.isEmpty())
        return watch(app, client, json);
    if (command == QLatin1String("integrations") && rest.isEmpty())
        return integrations(client, json);
    if (command == QLatin1String("settings") && rest.isEmpty())
        return settings(client, json);
    if (command == QLatin1String("settings") && rest.size() == 3 && rest.at(0) == QLatin1String("set"))
        return simple(client, QStringLiteral("SetSetting"), {rest.at(1), rest.at(2)},
                      QStringLiteral("%1 set").arg(rest.at(1)));
    if (command == QLatin1String("integration") && rest.size() == 2
        && (rest.at(0) == QLatin1String("enable") || rest.at(0) == QLatin1String("disable")))
        return simple(client, QStringLiteral("SetIntegrationEnabled"), {rest.at(1), rest.at(0) == QLatin1String("enable")},
                      QStringLiteral("done"), 60000);
    if (command == QLatin1String("integration") && rest.size() == 3 && rest.at(0) == QLatin1String("live-updates")
        && (rest.at(2) == QLatin1String("on") || rest.at(2) == QLatin1String("off")))
        return simple(client, QStringLiteral("SetIntegrationLiveUpdates"), {rest.at(1), rest.at(2) == QLatin1String("on")},
                      QStringLiteral("done"), 60000);
    if (command == QLatin1String("custom") && rest.size() == 2
        && (rest.at(0) == QLatin1String("add") || rest.at(0) == QLatin1String("remove"))) {
        const QString path = QFileInfo(rest.at(1)).absoluteFilePath();
        return simple(client, rest.at(0) == QLatin1String("add") ? QStringLiteral("RegisterCustomMetric")
                                                                  : QStringLiteral("UnregisterCustomMetric"),
                      {path}, rest.at(0) == QLatin1String("add") ? QStringLiteral("registered %1").arg(path)
                                                                  : QStringLiteral("unregistered %1").arg(path));
    }
    if (command == QLatin1String("custom") && rest.size() == 1 && rest.at(0) == QLatin1String("reload"))
        return reloadCustomMetrics(client, json);
    return usage(QStringLiteral("invalid command \"%1\"").arg((QStringList{command} + rest).join(u' ')));
}

#include "main.moc"
