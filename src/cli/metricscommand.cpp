#include "metricscommand.h"

#include "metrics/metricsformat.h"

#include <QStringList>
#include <QTime>

#include <chrono>
#include <csignal>
#include <algorithm>
#include <thread>

namespace aqtruncat::cli {

namespace {

using namespace aqtruncat::metrics;

volatile std::sig_atomic_t stopRequested = 0;

void onStopSignal(int)
{
    stopRequested = 1;
}

QString q(const std::string &s)
{
    return QString::fromStdString(s);
}

QString rate(const std::optional<double> &bytesPerSecond)
{
    return bytesPerSecond ? q(formatRate(*bytesPerSecond)) : QString::fromLatin1(UnavailableText);
}

QString notAvailable(const MetricStatus &status)
{
    const char *what = status.state == MetricState::Pending ? "waiting" : "n/a";
    return QStringLiteral("  %1: %2\n").arg(QLatin1String(what), q(status.message));
}

QString row(const char *label, const QString &value)
{
    return QStringLiteral("  %1 %2\n").arg(QLatin1String(label), -11).arg(value);
}

QString countedNames(const std::vector<InterfaceMetrics> &interfaces)
{
    QStringList names;
    for (const auto &i : interfaces)
        if (i.counted)
            names.append(q(i.name));
    return names.join(QStringLiteral(", "));
}

QString countedNames(const std::vector<DiskDeviceMetrics> &devices)
{
    QStringList names;
    for (const auto &d : devices)
        if (d.counted)
            names.append(q(d.name));
    return names.join(QStringLiteral(", "));
}

} // namespace

QString formatMetricsReport(const SystemMetricsSnapshot &s)
{
    QString out;

    out += QStringLiteral("CPU\n");
    if (s.cpu.status.ok()) {
        QString usage = q(formatPercent(*s.cpu.usagePercent));
        if (s.cpu.logicalCpus > 0)
            usage += QStringLiteral("  (%1 logical CPUs)").arg(s.cpu.logicalCpus);
        out += row("Usage:", usage);
    } else {
        out += notAvailable(s.cpu.status);
    }

    out += QStringLiteral("Memory\n");
    if (s.memory.status.ok()) {
        const MemoryInfo &m = *s.memory.info;
        out += row("Used:", QStringLiteral("%1 / %2  (%3)").arg(q(formatBytes(m.usedBytes)), q(formatBytes(m.totalBytes)),
                                                                 q(formatPercent(m.usagePercent))));
        out += row("Available:", q(formatBytes(m.availableBytes))
                                     + (m.availableIsEstimate ? QStringLiteral("  (estimated: no MemAvailable)") : QString()));
        out += QStringLiteral("Swap\n");
        if (m.swapPresent)
            out += row("Used:", QStringLiteral("%1 / %2  (%3)").arg(q(formatBytes(m.swapUsedBytes)),
                                                                     q(formatBytes(m.swapTotalBytes)),
                                                                     q(formatPercent(m.swapUsagePercent))));
        else
            out += QStringLiteral("  none\n");
    } else {
        out += notAvailable(s.memory.status);
    }

    out += QStringLiteral("Network");
    if (s.network.countedInterfaces > 0)
        out += QStringLiteral(" (%1)").arg(countedNames(s.network.interfaces));
    out += u'\n';
    if (s.network.status.ok()) {
        out += row("Download:", rate(s.network.downloadBytesPerSecond));
        out += row("Upload:", rate(s.network.uploadBytesPerSecond));
    } else {
        out += notAvailable(s.network.status);
    }

    for (const StorageMetrics &storage : s.storage) {
        out += QStringLiteral("Storage %1\n").arg(q(storage.path));
        if (storage.status.ok()) {
            const StorageInfo &st = *storage.info;
            out += row("Used:", QStringLiteral("%1 / %2  (%3)").arg(q(formatBytes(st.usedBytes)), q(formatBytes(st.totalBytes)),
                                                                     q(formatPercent(st.usagePercent))));
            out += row("Available:", q(formatBytes(st.availableBytes)));
        } else {
            out += notAvailable(storage.status);
        }
    }

    out += QStringLiteral("Disk I/O");
    if (s.diskIo.countedDevices > 0)
        out += QStringLiteral(" (%1)").arg(countedNames(s.diskIo.devices));
    out += u'\n';
    if (s.diskIo.status.ok()) {
        out += row("Read:", rate(s.diskIo.readBytesPerSecond));
        out += row("Write:", rate(s.diskIo.writeBytesPerSecond));
    } else {
        out += notAvailable(s.diskIo.status);
    }
    return out;
}

QString formatMetricsLine(const SystemMetricsSnapshot &s, const QString &clock)
{
    const QString na = QString::fromLatin1(UnavailableText);
    const QString cpu = s.cpu.status.ok() ? q(formatPercent(*s.cpu.usagePercent)) : na;
    const QString memory = s.memory.status.ok()
        ? QStringLiteral("%1 (%2)").arg(q(formatBytes(s.memory.info->usedBytes)), q(formatPercent(s.memory.info->usagePercent)))
        : na;
    const QString net = s.network.status.ok()
        ? QStringLiteral("down %1 up %2").arg(rate(s.network.downloadBytesPerSecond), rate(s.network.uploadBytesPerSecond))
        : na;
    const QString disk = s.diskIo.status.ok()
        ? QStringLiteral("read %1 write %2").arg(rate(s.diskIo.readBytesPerSecond), rate(s.diskIo.writeBytesPerSecond))
        : na;
    return QStringLiteral("%1  CPU %2 | Mem %3 | Net %4 | Disk %5").arg(clock, cpu, memory, net, disk);
}

int runMetrics(const Invocation &invocation, QTextStream &out, QTextStream &err, MetricsSources sources)
{
    using clock = std::chrono::steady_clock;
    SystemMetricsSampler sampler(std::move(sources));

    stopRequested = 0;
    std::signal(SIGINT, onStopSignal);
    std::signal(SIGTERM, onStopSignal);

    // Sleeps until `deadline` in short steps so Ctrl+C is handled promptly.
    auto sleepUntil = [](clock::time_point deadline) {
        while (!stopRequested && clock::now() < deadline)
            std::this_thread::sleep_for(std::min<clock::duration>(std::chrono::milliseconds(100), deadline - clock::now()));
        return !stopRequested;
    };

    const auto interval = std::chrono::seconds(1);
    auto next = clock::now();
    sampler.sample(SystemMetricsSampler::now()); // baseline for the rates
    next += interval;
    if (!sleepUntil(next))
        return ExitOk;

    if (!invocation.watch) {
        const SystemMetricsSnapshot snapshot = sampler.sample(SystemMetricsSampler::now());
        out << formatMetricsReport(snapshot);
        out.flush();
        const bool storageAvailable = std::any_of(snapshot.storage.cbegin(), snapshot.storage.cend(),
                                                  [](const StorageMetrics &storage) { return storage.status.ok(); });
        const bool nothing = !snapshot.cpu.status.ok() && !snapshot.memory.status.ok() && !snapshot.network.status.ok()
            && !snapshot.diskIo.status.ok() && !storageAvailable;
        if (nothing) {
            err << "aqtruncat: no system metric is available\n";
            err.flush();
            return ExitFailure;
        }
        return ExitOk;
    }

    while (!stopRequested) {
        const SystemMetricsSnapshot snapshot = sampler.sample(SystemMetricsSampler::now());
        out << formatMetricsLine(snapshot, QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))) << '\n';
        out.flush();
        next += interval; // fixed cadence; rates still use the measured elapsed time
        if (next < clock::now())
            next = clock::now() + interval; // fell behind (e.g. suspend): no burst of catch-up samples
        if (!sleepUntil(next))
            break;
    }
    return ExitOk;
}

} // namespace aqtruncat::cli
