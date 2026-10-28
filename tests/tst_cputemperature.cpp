#include "metrics/cputemperature.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace aqtruncat::metrics;

namespace {

class Fixture
{
public:
    QTemporaryDir temp;
    QString sys = temp.filePath(QStringLiteral("sys"));

    void write(const QString &path, const QByteArray &value)
    {
        const QFileInfo info(path);
        QDir().mkpath(info.path());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            qFatal("cannot write fixture %s", qPrintable(path));
        file.write(value);
    }

    QString hwmon(int index, const QString &driver, const QString &device = {})
    {
        const QString path = sys + QStringLiteral("/class/hwmon/hwmon%1").arg(index);
        write(path + QStringLiteral("/name"), driver.toUtf8() + '\n');
        if (!device.isEmpty()) {
            QDir().mkpath(device);
            if (!QFile::link(device, path + QStringLiteral("/device")))
                qFatal("cannot link fake hwmon device");
        }
        return path;
    }

    void channel(const QString &device, int number, const QByteArray &value, const QByteArray &label = {})
    {
        const QString prefix = device + QStringLiteral("/temp%1").arg(number);
        write(prefix + QStringLiteral("_input"), value + '\n');
        if (!label.isEmpty())
            write(prefix + QStringLiteral("_label"), label + '\n');
    }

    CpuTemperatureMetrics sample(double time = 0.0)
    {
        CpuTemperatureSampler sampler(sys.toStdString());
        return sampler.sample(time);
    }
};

} // namespace

class TestCpuTemperature : public QObject
{
    Q_OBJECT

private slots:
    void intelPackagePreferredOverCore()
    {
        Fixture f;
        const QString cpu = f.hwmon(4, QStringLiteral("coretemp"));
        f.channel(cpu, 1, "54250", "Package id 0");
        f.channel(cpu, 2, "61000", "Core 0");
        CpuTemperatureSampler sampler(f.sys.toStdString());
        const auto result = sampler.sample(1.0);
        QVERIFY(result.status.ok());
        QCOMPARE(*result.celsius, 54.25);
        QCOMPARE(result.sensor, "Package id 0");
    }

    void amdTdiePreferredOverTctl()
    {
        Fixture f;
        const QString cpu = f.hwmon(0, QStringLiteral("k10temp"));
        f.channel(cpu, 1, "70000", "Tctl");
        f.channel(cpu, 2, "62000", "Tdie");
        CpuTemperatureSampler sampler(f.sys.toStdString());
        const auto result = sampler.sample(1.0);
        QCOMPARE(*result.celsius, 62.0);
        QCOMPARE(result.sensor, "Tdie");
    }

    void knownCpuDriverWorksWithoutLabels()
    {
        Fixture f;
        const QString cpu = f.hwmon(0, QStringLiteral("coretemp"));
        f.channel(cpu, 1, "41000");
        CpuTemperatureSampler sampler(f.sys.toStdString());
        const auto result = sampler.sample(1.0);
        QVERIFY(result.status.ok());
        QCOMPARE(*result.celsius, 41.0);
    }

    void rejectsGpuNvmeAndMotherboardSensors()
    {
        Fixture f;
        for (const QString &driver : {QStringLiteral("amdgpu"), QStringLiteral("nvme"), QStringLiteral("nct6775")}) {
            const QString sensor = f.hwmon(int(driver.size()), driver);
            f.channel(sensor, 1, "55000", "CPU Package");
        }
        CpuTemperatureSampler sampler(f.sys.toStdString());
        const auto result = sampler.sample(1.0);
        QCOMPARE(result.status.state, MetricState::Unavailable);
        QVERIFY(!result.celsius);
    }

    void invalidAndMissingValuesNeverBecomeZero()
    {
        Fixture f;
        const QString cpu = f.hwmon(0, QStringLiteral("coretemp"));
        f.channel(cpu, 1, "not-a-number");
        CpuTemperatureSampler sampler(f.sys.toStdString());
        const auto result = sampler.sample(1.0);
        QCOMPARE(result.status.state, MetricState::Error);
        QVERIFY(!result.celsius);
    }

    void missingHwmonIsUnavailable()
    {
        Fixture f;
        CpuTemperatureSampler sampler(f.sys.toStdString());
        const auto result = sampler.sample(1.0);
        QCOMPARE(result.status.state, MetricState::Unavailable);
        QVERIFY(!result.celsius);
    }

    void cachesEmptyDiscoveryAndFindsLaterSensor()
    {
        Fixture f;
        CpuTemperatureSampler sampler(f.sys.toStdString());
        QCOMPARE(sampler.sample(1.0).status.state, MetricState::Unavailable);
        const QString cpu = f.hwmon(0, QStringLiteral("coretemp"));
        f.channel(cpu, 1, "43000");
        QCOMPARE(sampler.sample(2.0).status.state, MetricState::Unavailable);
        const auto rediscovered = sampler.sample(302.0);
        QVERIFY(rediscovered.status.ok());
        QCOMPARE(*rediscovered.celsius, 43.0);
    }

    void stableSelectionAndImmediateRediscovery()
    {
        Fixture f;
        const QString firstDevice = f.temp.filePath(QStringLiteral("devices/cpu-a"));
        const QString secondDevice = f.temp.filePath(QStringLiteral("devices/cpu-b"));
        QString first = f.hwmon(0, QStringLiteral("coretemp"), firstDevice);
        const QString second = f.hwmon(1, QStringLiteral("coretemp"), secondDevice);
        f.channel(first, 1, "50000", "Package id 0");
        f.channel(second, 1, "60000", "Package id 1");
        CpuTemperatureSampler sampler(f.sys.toStdString());
        QCOMPARE(*sampler.sample(1.0).celsius, 50.0);

        // Renumber hwmon after the initially selected sensor disappears.
        const QString moved = f.sys + QStringLiteral("/class/hwmon/hwmon7");
        QVERIFY(QDir().rename(first, moved));
        first = moved;
        f.channel(first, 1, "51000", "Package id 0");
        const auto afterReorder = sampler.sample(2.0);
        QVERIFY(afterReorder.status.ok());
        QCOMPARE(*afterReorder.celsius, 51.0);
        QCOMPARE(afterReorder.sensor, "Package id 0");

        QVERIFY(QDir(first).removeRecursively());
        const auto afterRemoval = sampler.sample(3.0);
        QVERIFY(afterRemoval.status.ok());
        QCOMPARE(*afterRemoval.celsius, 60.0);
    }

    void thermalZoneFallbackRequiresCpuIdentity()
    {
        Fixture f;
        const QString zone = f.sys + QStringLiteral("/class/thermal/thermal_zone9");
        f.write(zone + QStringLiteral("/type"), "x86_pkg_temp\n");
        f.write(zone + QStringLiteral("/temp"), "48000\n");
        CpuTemperatureSampler sampler(f.sys.toStdString());
        const auto result = sampler.sample(1.0);
        QVERIFY(result.status.ok());
        QCOMPARE(*result.celsius, 48.0);

        Fixture ambiguous;
        const QString acpi = ambiguous.sys + QStringLiteral("/class/thermal/thermal_zone0");
        ambiguous.write(acpi + QStringLiteral("/type"), "acpitz\n");
        ambiguous.write(acpi + QStringLiteral("/temp"), "44000\n");
        CpuTemperatureSampler noGuess(ambiguous.sys.toStdString());
        QCOMPARE(noGuess.sample(1.0).status.state, MetricState::Unavailable);
    }
};

QTEST_APPLESS_MAIN(TestCpuTemperature)
#include "tst_cputemperature.moc"
