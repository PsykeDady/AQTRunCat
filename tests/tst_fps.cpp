#include "metrics/fps.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>
#include <sys/time.h>

using namespace aqtruncat::metrics;

namespace {

const char Header[] = "fps,frametime,cpu_load,cpu_power,gpu_load,cpu_temp,gpu_temp,gpu_core_clock,gpu_mem_clock,"
                      "gpu_vram_used,gpu_power,ram_used,swap_used,process_rss,cpu_mhz,elapsed\n";

QByteArray row(double fps, double frametime)
{
    return QByteArray::number(fps, 'f', 1) + ',' + QByteArray::number(frametime, 'f', 3)
        + ",12,5,40,55,60,1500,800,1.2,70,8.1,0,1.5,3200,1000000000\n";
}

// A fake home with MangoHud's configuration and log folder, and a clock.
struct Fixture {
    QTemporaryDir tmp;
    double now = 1'800'000'000.0;
    QString logs() const { return tmp.filePath(QStringLiteral("logs")); }

    void write(const QString &relative, const QByteArray &content)
    {
        const QString path = tmp.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            qFatal("cannot create test fixture %s", qPrintable(path));
        f.write(content);
    }

    void touch(const QString &relative, double time)
    {
        const QByteArray path = tmp.filePath(relative).toLocal8Bit();
        struct timeval times[2];
        times[0].tv_sec = times[1].tv_sec = time_t(time);
        times[0].tv_usec = times[1].tv_usec = 0;
        ::utimes(path.constData(), times);
    }

    void configure() { write(QStringLiteral("config/MangoHud/MangoHud.conf"), "output_folder=~/logs\nlog_interval=100\n"); }
    void install() { write(QStringLiteral("layers/MangoHud.x86_64.json"), "{}"); }

    FpsSources sources()
    {
        FpsSources s;
        s.configDir = tmp.filePath(QStringLiteral("config/MangoHud")).toStdString();
        s.home = tmp.path().toStdString();
        s.layerDirs = {tmp.filePath(QStringLiteral("layers")).toStdString()};
        s.binaryDirs = {tmp.filePath(QStringLiteral("bin")).toStdString()};
        s.wallClock = [this] { return now; };
        return s;
    }
};

} // namespace

// F5.3: FPS only from MangoHud's own log, with explicit source conditions.
class TestFps : public QObject
{
    Q_OBJECT

private slots:
    void outputFoldersFromConfiguration()
    {
        const auto folders = mangoHudOutputFolders("# output_folder=/commented\n"
                                                   " output_folder = /data/logs \n"
                                                   "output_folder=~/mango\n"
                                                   "output_folder=\"/quoted path\"\n"
                                                   "output_folder=relative/ignored\n"
                                                   "output_folder=/data/logs\n"
                                                   "fps_limit=60\n",
                                                   "/home/u");
        QCOMPARE(folders, (std::vector<std::string>{"/data/logs", "/home/u/mango", "/quoted path"}));
    }

    void parsesTheLastCompleteRow()
    {
        std::string error;
        const QByteArray tail = QByteArray(Header) + row(59.9, 16.7) + row(143.6, 6.96) + "120.0,8.3,12,5"; // partial last line
        const auto parsed = parseMangoHudTail(Header, tail.toStdString(), &error);
        QVERIFY2(parsed, error.c_str());
        QCOMPARE(parsed->fps, 143.6);
        QCOMPARE(*parsed->frametimeMs, 6.96);
    }

    void tailStartingMidRowIsFine()
    {
        // The tail read starts anywhere in the file: the first fragment
        // is ignored because only the last complete row counts.
        std::string error;
        const QByteArray tail = "3200,1000\n" + row(75.0, 13.3);
        QCOMPARE(parseMangoHudTail(Header, tail.toStdString(), &error)->fps, 75.0);
    }

    void rejectsUnusableLogs()
    {
        std::string error;
        QVERIFY(!parseMangoHudTail("os,cpu,gpu\n", "a,b,c\n", &error));
        QVERIFY(QString::fromStdString(error).contains(QLatin1String("no fps column")));
        QVERIFY(!parseMangoHudTail(Header, Header, &error)); // header only
        QVERIFY(!parseMangoHudTail(Header, "", &error));
        QVERIFY(!parseMangoHudTail(Header, "abc,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15\n", &error));
        QVERIFY(QString::fromStdString(error).contains(QLatin1String("malformed")));
        QVERIFY(!parseMangoHudTail(Header, "-5,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15\n", &error));
        // A row with a locale's decimal comma has extra fields: an error, never a value.
        QVERIFY(!parseMangoHudTail(Header, "140,5,6,900,10,5,40,55,60,1500,800,1,2,70,8,1,0,1,5,3200,1\n", &error));
        QVERIFY(QString::fromStdString(error).contains(QLatin1String("malformed row")));
    }

    void applicationFromLogName()
    {
        QCOMPARE(mangoHudApplication("Cyberpunk2077_2026-10-22_21-04-11.csv"), std::string("Cyberpunk2077"));
        QCOMPARE(mangoHudApplication("vkcube_2026-01-02_03-04-05.csv"), std::string("vkcube"));
        QCOMPARE(mangoHudApplication("custom.csv"), std::string("custom"));
    }

    void disabledMissingAndNotConfigured()
    {
        Fixture f;
        MangoHudFpsReader reader(f.sources());
        FpsMetrics m = reader.sample(FpsSourceOff);
        QCOMPARE(m.condition, FpsCondition::Disabled);
        QCOMPARE(m.status.state, MetricState::Unavailable);

        m = reader.sample(FpsSourceMangoHud);
        QCOMPARE(m.condition, FpsCondition::Missing);
        QVERIFY(!m.fps);

        f.install();
        m = reader.sample(FpsSourceMangoHud);
        QCOMPARE(m.condition, FpsCondition::Missing); // the configuration is re-read every 5 s, not every sample
        f.now += 6;
        m = reader.sample(FpsSourceMangoHud);
        QCOMPARE(m.condition, FpsCondition::NotConfigured);
        QVERIFY(QString::fromStdString(m.status.message).contains(QLatin1String("output_folder")));
    }

    void inactiveMeasuredStaleAndInactiveAgain()
    {
        Fixture f;
        f.configure();
        QDir().mkpath(f.logs());
        MangoHudFpsReader reader(f.sources());
        QCOMPARE(reader.sample(FpsSourceMangoHud).condition, FpsCondition::Inactive); // no log yet

        f.write(QStringLiteral("logs/Game_2026-10-22_21-00-00.csv"), "v1\n0.7.2\n---SYSTEM INFO---\nos,cpu\nArch,i7\n"
                                                                    "---FRAME METRICS---\n" + QByteArray(Header)
                                                                        + row(60.0, 16.6) + row(144.2, 6.9));
        f.write(QStringLiteral("logs/Game_2026-10-22_20-00-00_summary.csv"), "0.1% Min FPS,Average FPS\n1,2\n");
        f.touch(QStringLiteral("logs/Game_2026-10-22_21-00-00.csv"), f.now - 1);
        f.touch(QStringLiteral("logs/Game_2026-10-22_20-00-00_summary.csv"), f.now); // newer but a summary
        FpsMetrics m = reader.sample(FpsSourceMangoHud);
        QCOMPARE(m.condition, FpsCondition::Measured);
        QVERIFY(m.status.ok());
        QCOMPARE(*m.fps, 144.2);
        QCOMPARE(*m.frametimeMs, 6.9);
        QCOMPARE(m.application, std::string("Game"));
        QCOMPARE(m.source, std::string("mangohud"));

        // Logging stopped: stale for a while (no current value), then inactive.
        f.now += 10;
        m = reader.sample(FpsSourceMangoHud);
        QCOMPARE(m.condition, FpsCondition::Stale);
        QCOMPARE(m.status.state, MetricState::Unavailable);
        QVERIFY(!m.fps);
        QCOMPARE(*m.lastFps, 144.2);
        f.now += InactiveSeconds;
        m = reader.sample(FpsSourceMangoHud);
        QCOMPARE(m.condition, FpsCondition::Inactive);
        QVERIFY(!m.fps && !m.lastFps);
    }

    void followsTheNewestLog()
    {
        Fixture f;
        f.configure();
        f.write(QStringLiteral("logs/Old_2026-10-22_10-00-00.csv"), QByteArray(Header) + row(30.0, 33.3));
        f.touch(QStringLiteral("logs/Old_2026-10-22_10-00-00.csv"), f.now - 2);
        MangoHudFpsReader reader(f.sources());
        QCOMPARE(reader.sample(FpsSourceMangoHud).application, std::string("Old"));

        f.write(QStringLiteral("logs/New_2026-10-22_11-00-00.csv"), QByteArray(Header) + row(90.0, 11.1));
        f.touch(QStringLiteral("logs/New_2026-10-22_11-00-00.csv"), f.now);
        f.touch(QStringLiteral("logs"), f.now); // a new file changes the folder
        const FpsMetrics m = reader.sample(FpsSourceMangoHud);
        QCOMPARE(m.application, std::string("New"));
        QCOMPARE(*m.fps, 90.0);
    }

    void unreadableFormatIsAnErrorNotAValue()
    {
        Fixture f;
        f.configure();
        f.write(QStringLiteral("logs/Game_2026-10-22_21-00-00.csv"), "something,else\n1,2\n");
        f.touch(QStringLiteral("logs/Game_2026-10-22_21-00-00.csv"), f.now);
        MangoHudFpsReader reader(f.sources());
        const FpsMetrics m = reader.sample(FpsSourceMangoHud);
        QCOMPARE(m.condition, FpsCondition::Unsupported);
        QCOMPARE(m.status.state, MetricState::Error);
        QVERIFY(!m.fps);
    }

    void headerWithoutRowsIsWaiting()
    {
        Fixture f;
        f.configure();
        f.write(QStringLiteral("logs/Game_2026-10-22_21-00-00.csv"), QByteArray(Header));
        f.touch(QStringLiteral("logs/Game_2026-10-22_21-00-00.csv"), f.now);
        MangoHudFpsReader reader(f.sources());
        const FpsMetrics m = reader.sample(FpsSourceMangoHud);
        QCOMPARE(m.status.state, MetricState::Pending);
        QVERIFY(!m.fps);
    }
};

QTEST_GUILESS_MAIN(TestFps)
#include "tst_fps.moc"
