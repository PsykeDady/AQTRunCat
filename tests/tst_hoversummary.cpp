// F6: the hover summary of the Qt tray (src/app/hoversummary.*): text from
// a snapshot, missing values in words, backend states, units and rounding.
#include "app/hoversummary.h"
#include "metrics/metricsformat.h"

#include <QDateTime>
#include <QTest>

using namespace aqtruncat;
using aqtruncat::hover::Connection;
using aqtruncat::metrics::MetricStatus;
using aqtruncat::metrics::SystemMetricsSnapshot;

namespace {

constexpr std::uint64_t GiB = 1ull << 30;
constexpr std::uint64_t MiB = 1ull << 20;

SystemMetricsSnapshot snapshot()
{
    SystemMetricsSnapshot s;
    s.cpu.status = MetricStatus::okStatus();
    s.cpu.usagePercent = 18.44;
    s.memory.status = MetricStatus::okStatus();
    metrics::MemoryInfo m;
    m.totalBytes = std::uint64_t(15.5 * GiB);
    m.usedBytes = std::uint64_t(7.2 * GiB);
    m.availableBytes = m.totalBytes - m.usedBytes;
    m.usagePercent = 100.0 * double(m.usedBytes) / double(m.totalBytes);
    m.swapPresent = true;
    m.swapTotalBytes = 12 * GiB;
    m.swapUsedBytes = 300 * MiB;
    m.swapUsagePercent = 100.0 * double(m.swapUsedBytes) / double(m.swapTotalBytes);
    s.memory.info = m;
    return s;
}

} // namespace

class TestHoverSummary : public QObject
{
    Q_OBJECT

private slots:
    void validValues()
    {
        const SystemMetricsSnapshot s = snapshot();
        const QStringList lines = hover::summaryLines(Connection::Connected, &s);
        QCOMPARE(lines, (QStringList{QStringLiteral("CPU: 18.4 %"),
                                     QStringLiteral("RAM: 46.5 % · 7.2 GiB / 15.5 GiB used"),
                                     QStringLiteral("Swap: 2.4 % · 300.0 MiB / 12.0 GiB")}));
        QCOMPARE(hover::runnerToolTip(QStringLiteral("Running Cat"), lines),
                 QStringLiteral("AQTRunCat — Running Cat\nCPU: 18.4 %\nRAM: 46.5 % · 7.2 GiB / 15.5 GiB used\n"
                                "Swap: 2.4 % · 300.0 MiB / 12.0 GiB"));
    }

    void unitsAndRounding()
    {
        SystemMetricsSnapshot s = snapshot();
        s.cpu.usagePercent = 99.96; // one decimal, no 99.9 cap
        auto &m = *s.memory.info;
        m.usedBytes = 1023 * MiB + 1000 * 1024; // just under 1 GiB: shown as 1.0 GiB, never "1024.0 MiB"
        m.totalBytes = 2 * GiB;
        m.usagePercent = 49.99;
        m.swapUsedBytes = 0; // a genuine zero is a value
        m.swapUsagePercent = 0.0;
        const QStringList lines = hover::summaryLines(Connection::Connected, &s);
        QCOMPARE(lines.value(0), QStringLiteral("CPU: 100.0 %"));
        QCOMPARE(lines.value(1), QStringLiteral("RAM: 50.0 % · 1.0 GiB / 2.0 GiB used"));
        QCOMPARE(lines.value(2), QStringLiteral("Swap: 0.0 % · 0 B / 12.0 GiB"));
    }

    void estimatedMemory()
    {
        SystemMetricsSnapshot s = snapshot();
        s.memory.info->availableIsEstimate = true;
        QVERIFY(hover::summaryLines(Connection::Connected, &s).value(1).endsWith(QStringLiteral("used (estimated)")));
    }

    void noSwapHasNoLine()
    {
        SystemMetricsSnapshot s = snapshot();
        s.memory.info->swapPresent = false;
        s.memory.info->swapTotalBytes = 0;
        const QStringList lines = hover::summaryLines(Connection::Connected, &s);
        QCOMPARE(lines.size(), 2);
        QVERIFY(!lines.join(u'\n').contains(QStringLiteral("Swap")));
    }

    void missingValuesAreWords()
    {
        SystemMetricsSnapshot s = snapshot();
        s.cpu.status = MetricStatus::pending("waiting for the next sample");
        s.cpu.usagePercent.reset();
        s.memory.status = MetricStatus::error("cannot read /proc/meminfo: Permission denied");
        s.memory.info.reset();
        QStringList lines = hover::summaryLines(Connection::Connected, &s);
        QCOMPARE(lines, (QStringList{QStringLiteral("CPU: waiting for data"), QStringLiteral("RAM: error"),
                                     QStringLiteral("Swap: error")}));
        // Technical messages stay in the Dashboard.
        QVERIFY(!lines.join(u'\n').contains(QStringLiteral("/proc")));

        s.cpu.status = MetricStatus::unavailable("no /proc/stat");
        s.memory.status = MetricStatus::unavailable("no /proc/meminfo");
        lines = hover::summaryLines(Connection::Connected, &s);
        QCOMPARE(lines.value(0), QStringLiteral("CPU: unavailable"));
        QCOMPARE(lines.value(1), QStringLiteral("RAM: unavailable"));

        // Ok without a value (first sample): waiting, never 0.
        s.cpu.status = MetricStatus::okStatus();
        s.memory.status = MetricStatus::okStatus();
        lines = hover::summaryLines(Connection::Connected, &s);
        QCOMPARE(lines.value(0), QStringLiteral("CPU: waiting for data"));
        QCOMPARE(lines.value(1), QStringLiteral("RAM: waiting for data"));
        QVERIFY(!lines.join(u'\n').contains(QStringLiteral("0.0")));
    }

    void backendStates()
    {
        const SystemMetricsSnapshot s = snapshot();
        QCOMPARE(hover::summaryLines(Connection::Connecting, nullptr), QStringList{hover::ConnectingText});
        QCOMPARE(hover::summaryLines(Connection::Connected, nullptr), QStringList{hover::ConnectingText});
        // A lost backend: no values, even if a snapshot is still around.
        QCOMPARE(hover::summaryLines(Connection::Unavailable, &s), QStringList{hover::UnavailableText});
        QCOMPARE(hover::runnerToolTip(QString(), {hover::UnavailableText}),
                 QStringLiteral("AQTRunCat\nBackend not running"));
    }

    void sharesTheApiDetailTexts()
    {
        // The Plasmoid shows the backend's "detail" after the percentage;
        // both come from the same formatter.
        const auto m = *snapshot().memory.info;
        QCOMPARE(QString::fromStdString(metrics::formatMemoryDetail(m)), QStringLiteral("7.2 GiB / 15.5 GiB used"));
        QCOMPARE(QString::fromStdString(metrics::formatSwapDetail(m)), QStringLiteral("300.0 MiB / 12.0 GiB"));
    }

    void whenText()
    {
        const QDateTime today(QDate::currentDate(), QTime(14, 5));
        QCOMPARE(hover::whenText(today.toSecsSinceEpoch()), QStringLiteral("14:05"));
        const QDateTime earlier(QDate(2026, 10, 2), QTime(9, 30));
        if (earlier.date() != QDate::currentDate())
            QCOMPARE(hover::whenText(earlier.toSecsSinceEpoch()), QStringLiteral("Oct 2, 09:30"));
    }
};

QTEST_GUILESS_MAIN(TestHoverSummary)
#include "tst_hoversummary.moc"
