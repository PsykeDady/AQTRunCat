#include "metrics/systeminfo.h"
#include "metrics/systemmetrics.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace aqtruncat::metrics;

namespace {

void write(const QString &path, const QByteArray &content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        qFatal("cannot create test fixture %s", qPrintable(path));
    file.write(content);
}

} // namespace

// F5.6: kernel (uname) and distribution (os-release as data, fallbacks).
class TestSystemInfo : public QObject
{
    Q_OBJECT

private slots:
    void parsesOsReleaseAsData()
    {
        int malformed = 0;
        const auto values = parseOsRelease(
            "# comment\n"
            "NAME=\"Fedora Linux\"\n"
            "VERSION='44 (Workstation Edition)'\n"
            "ID=fedora\n"
            "ID_LIKE=\"rhel centos\"\n"
            "PRETTY_NAME=\"Fedora \\\"Linux\\\" 44 \\\\ \\$HOME \\`x\\`\"\n"
            "\n"
            "   VERSION_ID=44   \n",
            &malformed);
        QCOMPARE(values.at("NAME"), std::string("Fedora Linux"));
        QCOMPARE(values.at("VERSION"), std::string("44 (Workstation Edition)"));
        QCOMPARE(values.at("ID"), std::string("fedora"));
        QCOMPARE(values.at("ID_LIKE"), std::string("rhel centos"));
        QCOMPARE(values.at("PRETTY_NAME"), std::string("Fedora \"Linux\" 44 \\ $HOME `x`"));
        QCOMPARE(values.at("VERSION_ID"), std::string("44"));
        QCOMPARE(malformed, 0);
    }

    void neverExecutesOrExpandsAnything()
    {
        // Shell syntax is just text, or a malformed (skipped) line.
        int malformed = 0;
        const auto values = parseOsRelease("NAME=$(touch /tmp/pwned)\n"
                                           "ID=`id`\n"
                                           "VERSION=\"$(id)\"\n"
                                           "PRETTY_NAME=\"unterminated\n"
                                           "BUILD_ID=a b\n"
                                           "lower=case\n"
                                           "NOEQUALS\n"
                                           "VARIANT=\"x\" trailing\n",
                                           &malformed);
        QVERIFY(!values.contains("NAME"));
        QVERIFY(!values.contains("ID"));
        QCOMPARE(values.at("VERSION"), std::string("$(id)")); // quoted: literal text
        QVERIFY(!values.contains("PRETTY_NAME"));
        QVERIFY(!values.contains("BUILD_ID"));
        QVERIFY(!values.contains("lower"));
        QVERIFY(!values.contains("VARIANT"));
        QCOMPARE(malformed, 7);
    }

    void readsEtcOsRelease()
    {
        QTemporaryDir root;
        write(root.filePath(QStringLiteral("etc/os-release")),
              "NAME=\"Arch Linux\"\nPRETTY_NAME=\"Arch Linux\"\nID=arch\nBUILD_ID=rolling\n");
        const DistributionMetrics d = readDistribution(root.path().toStdString());
        QVERIFY(d.status.ok());
        QCOMPARE(d.info->prettyName, std::string("Arch Linux"));
        QCOMPARE(d.info->id, std::string("arch"));
        QCOMPARE(d.info->buildId, std::string("rolling"));
        QCOMPARE(d.info->source, std::string("/etc/os-release"));
        QVERIFY(d.info->idLike.empty());
    }

    void fallsBackToUsrLibOsRelease()
    {
        QTemporaryDir root;
        write(root.filePath(QStringLiteral("usr/lib/os-release")),
              "NAME=Debian\nVERSION_ID=\"13\"\nVERSION=\"13 (trixie)\"\nVERSION_CODENAME=trixie\nID=debian\n");
        const DistributionMetrics d = readDistribution(root.path().toStdString());
        QVERIFY(d.status.ok());
        QCOMPARE(d.info->source, std::string("/usr/lib/os-release"));
        // No PRETTY_NAME: NAME + VERSION.
        QCOMPARE(d.info->prettyName, std::string("Debian 13 (trixie)"));
        QCOMPARE(d.info->codename, std::string("trixie"));
    }

    void unusableOsReleaseFallsThrough()
    {
        QTemporaryDir root;
        write(root.filePath(QStringLiteral("etc/os-release")), "garbage\n\x01\x02\n");
        write(root.filePath(QStringLiteral("etc/lsb-release")),
              "DISTRIB_ID=Ubuntu\nDISTRIB_RELEASE=26.04\nDISTRIB_CODENAME=resolute\nDISTRIB_DESCRIPTION=\"Ubuntu 26.04 LTS\"\n");
        const DistributionMetrics d = readDistribution(root.path().toStdString());
        QVERIFY(d.status.ok());
        QCOMPARE(d.info->source, std::string("/etc/lsb-release"));
        QCOMPARE(d.info->prettyName, std::string("Ubuntu 26.04 LTS"));
        QCOMPARE(d.info->id, std::string("ubuntu"));
        QCOMPARE(d.info->versionId, std::string("26.04"));
    }

    void distributionSpecificFiles_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QByteArray>("content");
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("pretty");
        QTest::newRow("arch") << "etc/arch-release" << QByteArray() << "arch" << "Arch Linux";
        QTest::newRow("fedora") << "etc/fedora-release" << QByteArray("Fedora release 44 (Forty Four)\n") << "fedora"
                                << "Fedora release 44 (Forty Four)";
        QTest::newRow("debian") << "etc/debian_version" << QByteArray("13.1\n") << "debian" << "Debian 13.1";
    }
    void distributionSpecificFiles()
    {
        QFETCH(QString, file);
        QFETCH(QByteArray, content);
        QFETCH(QString, id);
        QFETCH(QString, pretty);
        QTemporaryDir root;
        write(root.filePath(file), content);
        const DistributionMetrics d = readDistribution(root.path().toStdString());
        QVERIFY(d.status.ok());
        QCOMPARE(QString::fromStdString(d.info->id), id);
        QCOMPARE(QString::fromStdString(d.info->prettyName), pretty);
        QCOMPARE(QString::fromStdString(d.info->source), u'/' + file);
    }

    void nothingKnownIsUnavailableNotAFailure()
    {
        QTemporaryDir root;
        const DistributionMetrics d = readDistribution(root.path().toStdString());
        QCOMPARE(d.status.state, MetricState::Unavailable);
        QVERIFY(!d.info);
    }

    void oversizedReleaseFileIsIgnored()
    {
        QTemporaryDir root;
        write(root.filePath(QStringLiteral("etc/os-release")), QByteArray(200 * 1024, 'A'));
        write(root.filePath(QStringLiteral("etc/arch-release")), {});
        const DistributionMetrics d = readDistribution(root.path().toStdString());
        QCOMPARE(d.info->source, std::string("/etc/arch-release"));
    }

    void kernelFromUname()
    {
        std::string error;
        const auto kernel = queryKernel(&error);
        QVERIFY2(kernel, error.c_str());
        QCOMPARE(kernel->sysname, std::string("Linux"));
        QVERIFY(!kernel->release.empty());
        QVERIFY(!kernel->machine.empty());
    }

    void samplerKeepsKernelAndDistributionSeparateAndRefreshesRarely()
    {
        QTemporaryDir root;
        write(root.filePath(QStringLiteral("etc/os-release")), "PRETTY_NAME=\"One\"\nID=one\n");
        int kernelReads = 0;
        MetricsSources sources;
        sources.procRoot = root.filePath(QStringLiteral("proc")).toStdString();
        sources.sysRoot = root.filePath(QStringLiteral("sys")).toStdString();
        sources.osRoot = root.path().toStdString();
        sources.fpsSource = FpsSourceOff;
        sources.kernel = [&kernelReads](std::string *) -> std::optional<KernelInfo> {
            ++kernelReads;
            return KernelInfo{"Linux", "6.1.0-test", "#1 SMP", "aarch64"};
        };
        SystemMetricsSampler sampler(sources);
        SystemMetricsSnapshot s = sampler.sample(100.0);
        QCOMPARE(s.kernel.info->release, std::string("6.1.0-test"));
        QCOMPARE(s.kernel.info->machine, std::string("aarch64"));
        QCOMPARE(s.distribution.info->prettyName, std::string("One"));
        write(root.filePath(QStringLiteral("etc/os-release")), "PRETTY_NAME=\"Two\"\nID=two\n");
        s = sampler.sample(101.0);
        QCOMPARE(kernelReads, 1); // not re-read every second
        QCOMPARE(s.distribution.info->prettyName, std::string("One"));
        s = sampler.sample(100.0 + InfoRefreshSeconds);
        QCOMPARE(kernelReads, 2);
        QCOMPARE(s.distribution.info->prettyName, std::string("Two"));

        sources.kernel = [](std::string *error) -> std::optional<KernelInfo> {
            *error = "uname: not permitted";
            return std::nullopt;
        };
        SystemMetricsSampler failing(sources);
        s = failing.sample(1.0);
        QCOMPARE(s.kernel.status.state, MetricState::Unavailable);
        QCOMPARE(s.kernel.status.message, std::string("uname: not permitted"));
        QVERIFY(s.distribution.status.ok()); // independent
    }
};

QTEST_GUILESS_MAIN(TestSystemInfo)
#include "tst_systeminfo.moc"
