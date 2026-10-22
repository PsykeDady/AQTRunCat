#include "metrics/gpu.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace aqtruncat::metrics;

namespace {

// A /sys look-alike with DRM cards; no real GPU is ever read.
struct FakeSys {
    QTemporaryDir tmp;
    QString root() const { return tmp.path(); }

    void file(const QString &relative, const QByteArray &content)
    {
        const QString path = tmp.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            qFatal("cannot create test fixture %s", qPrintable(path));
        f.write(content);
    }

    // card<N> with its PCI device directory.
    QString card(int n, const QByteArray &slot, const QByteArray &driver, const QByteArray &vendor, const QByteArray &device,
                 const QByteArray &pciClass = "0x030000")
    {
        const QString dev = QStringLiteral("class/drm/card%1/device").arg(n);
        file(dev + QStringLiteral("/uevent"), "DRIVER=" + driver + "\nPCI_CLASS=30000\nPCI_SLOT_NAME=" + slot + "\n");
        file(dev + QStringLiteral("/vendor"), vendor + "\n");
        file(dev + QStringLiteral("/device"), device + "\n");
        file(dev + QStringLiteral("/class"), pciClass + "\n");
        file(dev + QStringLiteral("/power/runtime_status"), "active\n");
        // A connector entry, which must not count as a GPU.
        QDir().mkpath(tmp.filePath(QStringLiteral("class/drm/card%1-eDP-1").arg(n)));
        return dev;
    }

    std::string path() const { return tmp.path().toStdString(); }
};

const NvmlQuery noNvml = [](const std::string &, std::string *error) -> std::optional<NvmlReading> {
    *error = "NVML (libnvidia-ml.so.1, part of the NVIDIA driver) is not installed";
    return std::nullopt;
};

const GpuDevice *byId(const GpuMetrics &metrics, const std::string &id)
{
    for (const GpuDevice &gpu : metrics.devices) {
        if (gpu.id == id)
            return &gpu;
    }
    return nullptr;
}

} // namespace

// F5.2: discovery, vendor telemetry, per-field availability, selection.
class TestGpu : public QObject
{
    Q_OBJECT

private slots:
    void noGpuIsUnavailable()
    {
        FakeSys sys;
        GpuSampler sampler(sys.path(), {}, noNvml);
        const GpuMetrics metrics = sampler.sample(1.0, AutoGpu);
        QCOMPARE(metrics.status.state, MetricState::Unavailable);
        QVERIFY(metrics.devices.empty());
    }

    void amdgpuReportsEveryField()
    {
        FakeSys sys;
        const QString dev = sys.card(0, "0000:03:00.0", "amdgpu", "0x1002", "0x73bf");
        sys.file(dev + QStringLiteral("/gpu_busy_percent"), "37\n");
        sys.file(dev + QStringLiteral("/mem_info_vram_used"), "2147483648\n");
        sys.file(dev + QStringLiteral("/mem_info_vram_total"), "17163091968\n");
        sys.file(dev + QStringLiteral("/hwmon/hwmon3/temp1_label"), "edge\n");
        sys.file(dev + QStringLiteral("/hwmon/hwmon3/temp1_input"), "54000\n");
        sys.file(dev + QStringLiteral("/hwmon/hwmon3/temp2_label"), "junction\n");
        sys.file(dev + QStringLiteral("/hwmon/hwmon3/temp2_input"), "61000\n");
        sys.file(dev + QStringLiteral("/pp_dpm_sclk"), "0: 500Mhz \n1: 2100Mhz *\n2: 2500Mhz \n");
        sys.file(dev + QStringLiteral("/product_name"), "Radeon RX 6800 XT\n");
        GpuSampler sampler(sys.path(), {}, noNvml);
        const GpuMetrics metrics = sampler.sample(1.0, AutoGpu);
        QVERIFY(metrics.status.ok());
        QCOMPARE(metrics.devices.size(), std::size_t(1)); // the connector is not a GPU
        const GpuDevice &gpu = metrics.devices.front();
        QCOMPARE(gpu.id, std::string("0000:03:00.0"));
        QCOMPARE(gpu.card, std::string("card0"));
        QCOMPARE(gpu.vendor, std::string("AMD"));
        QCOMPARE(gpu.model, std::string("AMD Radeon RX 6800 XT"));
        QVERIFY(!gpu.integrated); // AMD: the bus does not say
        QVERIFY(gpu.utilizationStatus.ok());
        QCOMPARE(*gpu.utilizationPercent, 37.0);
        QCOMPARE(gpu.utilizationMethod, std::string("busy-percent"));
        QCOMPARE(*gpu.memoryUsedBytes, std::uint64_t(2147483648));
        QCOMPARE(*gpu.memoryTotalBytes, std::uint64_t(17163091968));
        QCOMPARE(*gpu.temperatureCelsius, 54.0);
        QCOMPARE(gpu.temperatureSensor, std::string("edge"));
        QCOMPARE(*gpu.frequencyMHz, 2100.0);
        QVERIFY(gpu.selected);
    }

    void missingFieldsAreUnavailableNeverZero()
    {
        FakeSys sys;
        sys.card(0, "0000:03:00.0", "amdgpu", "0x1002", "0x73bf"); // no telemetry files
        GpuSampler sampler(sys.path(), {}, noNvml);
        const GpuDevice gpu = sampler.sample(1.0, AutoGpu).devices.front();
        QCOMPARE(gpu.utilizationStatus.state, MetricState::Unavailable);
        QVERIFY(!gpu.utilizationPercent);
        QCOMPARE(gpu.memoryStatus.state, MetricState::Unavailable);
        QVERIFY(!gpu.memoryUsedBytes && !gpu.memoryTotalBytes);
        QCOMPARE(gpu.temperatureStatus.state, MetricState::Unavailable);
        QVERIFY(!gpu.temperatureCelsius);
        QCOMPARE(gpu.model, std::string("AMD GPU [1002:73bf]")); // no pci.ids, no product name
    }

    void intelActivityFromRc6Residency()
    {
        FakeSys sys;
        const QString dev = sys.card(1, "0000:00:02.0", "i915", "0x8086", "0x9a49");
        sys.file(QStringLiteral("class/drm/card1/gt/gt0/rc6_residency_ms"), "10000\n");
        sys.file(QStringLiteral("class/drm/card1/gt_act_freq_mhz"), "350\n");
        GpuSampler sampler(sys.path(), {}, noNvml);
        GpuDevice gpu = sampler.sample(100.0, AutoGpu).devices.front();
        QVERIFY(gpu.integrated && *gpu.integrated);
        QCOMPARE(gpu.utilizationStatus.state, MetricState::Pending); // needs a second sample
        QVERIFY(!gpu.utilizationPercent);
        QCOMPARE(gpu.memoryStatus.state, MetricState::Unavailable);
        QVERIFY(QString::fromStdString(gpu.memoryStatus.message).contains(QLatin1String("shared system memory")));
        QCOMPARE(gpu.temperatureStatus.state, MetricState::Unavailable);
        QCOMPARE(*gpu.frequencyMHz, 350.0);

        // 2 s later, 1.5 s of them idle in RC6: 25 % active.
        sys.file(QStringLiteral("class/drm/card1/gt/gt0/rc6_residency_ms"), "11500\n");
        gpu = sampler.sample(102.0, AutoGpu).devices.front();
        QVERIFY(gpu.utilizationStatus.ok());
        QCOMPARE(gpu.utilizationMethod, std::string("rc6-residency"));
        QVERIFY(qAbs(*gpu.utilizationPercent - 25.0) < 1e-9);

        // A counter that goes back (reset, suspend): no value, new baseline.
        sys.file(QStringLiteral("class/drm/card1/gt/gt0/rc6_residency_ms"), "100\n");
        gpu = sampler.sample(103.0, AutoGpu).devices.front();
        QCOMPARE(gpu.utilizationStatus.state, MetricState::Pending);
        sys.file(QStringLiteral("class/drm/card1/gt/gt0/rc6_residency_ms"), "1100\n");
        gpu = sampler.sample(104.0, AutoGpu).devices.front();
        QCOMPARE(*gpu.utilizationPercent, 0.0); // fully idle is a real 0
    }

    void xeIdleResidencyAndHwmonTemperature()
    {
        FakeSys sys;
        const QString dev = sys.card(0, "0000:03:00.0", "xe", "0x8086", "0x56a0");
        sys.file(dev + QStringLiteral("/tile0/gt0/gtidle/idle_residency_ms"), "0\n");
        sys.file(dev + QStringLiteral("/hwmon/hwmon5/temp2_label"), "pkg\n");
        sys.file(dev + QStringLiteral("/hwmon/hwmon5/temp2_input"), "47500\n");
        GpuSampler sampler(sys.path(), {}, noNvml);
        sampler.sample(1.0, AutoGpu);
        sys.file(dev + QStringLiteral("/tile0/gt0/gtidle/idle_residency_ms"), "0\n");
        const GpuDevice gpu = sampler.sample(2.0, AutoGpu).devices.front();
        QVERIFY(gpu.integrated && !*gpu.integrated); // not on the root bus
        QCOMPARE(*gpu.utilizationPercent, 100.0);
        QCOMPARE(*gpu.temperatureCelsius, 47.5);
        QCOMPARE(gpu.temperatureSensor, std::string("pkg"));
        QCOMPARE(gpu.memoryStatus.state, MetricState::Unavailable);
    }

    void nvidiaThroughNvml()
    {
        FakeSys sys;
        sys.card(0, "0000:01:00.0", "nvidia", "0x10de", "0x2684");
        QStringList queried;
        const NvmlQuery nvml = [&queried](const std::string &slot, std::string *) -> std::optional<NvmlReading> {
            queried << QString::fromStdString(slot);
            NvmlReading reading;
            reading.name = "GeForce RTX 4090";
            reading.utilizationPercent = 88.0;
            reading.memoryUsedBytes = 4ull << 30;
            reading.memoryTotalBytes = 24ull << 30;
            return reading; // no temperature
        };
        GpuSampler sampler(sys.path(), {}, nvml);
        const GpuDevice gpu = sampler.sample(1.0, AutoGpu).devices.front();
        QCOMPARE(queried, QStringList{QStringLiteral("0000:01:00.0")});
        QCOMPARE(gpu.model, std::string("NVIDIA GeForce RTX 4090"));
        QCOMPARE(*gpu.utilizationPercent, 88.0);
        QCOMPARE(gpu.utilizationMethod, std::string("nvml"));
        QCOMPARE(*gpu.memoryTotalBytes, std::uint64_t(24ull << 30));
        QCOMPARE(gpu.temperatureStatus.state, MetricState::Unavailable);
        QVERIFY(gpu.integrated && !*gpu.integrated);
    }

    void nvidiaWithoutNvmlKeepsTheDevice()
    {
        FakeSys sys;
        sys.card(0, "0000:01:00.0", "nvidia", "0x10de", "0x2684");
        GpuSampler sampler(sys.path(), {}, noNvml);
        const GpuMetrics metrics = sampler.sample(1.0, AutoGpu);
        QVERIFY(metrics.status.ok());
        const GpuDevice &gpu = metrics.devices.front();
        QCOMPARE(gpu.utilizationStatus.state, MetricState::Unavailable);
        QVERIFY(QString::fromStdString(gpu.utilizationStatus.message).contains(QLatin1String("NVML")));
        QVERIFY(!gpu.utilizationPercent && !gpu.memoryUsedBytes && !gpu.temperatureCelsius);
    }

    void suspendedGpuIsNotWokenUp()
    {
        FakeSys sys;
        const QString dev = sys.card(0, "0000:01:00.0", "nvidia", "0x10de", "0x2684");
        sys.file(dev + QStringLiteral("/power/runtime_status"), "suspended\n");
        bool asked = false;
        const NvmlQuery nvml = [&asked](const std::string &, std::string *) -> std::optional<NvmlReading> {
            asked = true;
            return NvmlReading{};
        };
        GpuSampler sampler(sys.path(), {}, nvml);
        const GpuDevice gpu = sampler.sample(1.0, AutoGpu).devices.front();
        QVERIFY(!asked); // NVML would wake the GPU up
        QVERIFY(gpu.suspended);
        for (const MetricStatus *status : {&gpu.utilizationStatus, &gpu.memoryStatus, &gpu.temperatureStatus}) {
            QCOMPARE(status->state, MetricState::Unavailable);
            QVERIFY(QString::fromStdString(status->message).contains(QLatin1String("suspended")));
        }
    }

    void hybridLaptopOrderAndSelection()
    {
        FakeSys sys;
        // Created in "wrong" order: the result is sorted by device id.
        const QString amd = sys.card(1, "0000:01:00.0", "amdgpu", "0x1002", "0x73ff");
        sys.file(amd + QStringLiteral("/gpu_busy_percent"), "3\n");
        sys.card(0, "0000:00:02.0", "i915", "0x8086", "0x46a6");
        GpuSampler sampler(sys.path(), {}, noNvml);

        GpuMetrics metrics = sampler.sample(1.0, AutoGpu);
        QCOMPARE(metrics.devices.size(), std::size_t(2));
        QCOMPARE(metrics.devices[0].id, std::string("0000:00:02.0"));
        QCOMPARE(metrics.devices[1].id, std::string("0000:01:00.0"));
        // Auto: the first discrete GPU; AMD's kind is unknown, Intel's iGPU
        // is integrated, so no known discrete → the first GPU.
        QVERIFY(metrics.devices[0].selected);
        QVERIFY(!metrics.devices[1].selected);

        metrics = sampler.sample(2.0, "0000:01:00.0");
        QVERIFY(byId(metrics, "0000:01:00.0")->selected);
        QVERIFY(!byId(metrics, "0000:00:02.0")->selected);

        // A preferred GPU that is gone falls back to auto (the setting stays).
        metrics = sampler.sample(3.0, "0000:09:00.0");
        QVERIFY(metrics.devices[0].selected);
    }

    void autoPrefersAKnownDiscreteGpu()
    {
        std::vector<GpuDevice> devices(2);
        devices[0].id = "0000:00:02.0";
        devices[0].integrated = true;
        devices[1].id = "0000:01:00.0";
        devices[1].integrated = false;
        QCOMPARE(selectGpu(devices, AutoGpu), 1);
        QCOMPARE(selectGpu(devices, ""), 1);
        QCOMPARE(selectGpu(devices, "0000:00:02.0"), 0);
        std::vector<GpuDevice> none;
        QCOMPARE(selectGpu(none, AutoGpu), -1);
    }

    void firmwareFramebuffersAndNonDisplayDevicesAreSkipped()
    {
        FakeSys sys;
        sys.file(QStringLiteral("class/drm/card0/device/uevent"), "DRIVER=simpledrm\n");
        sys.card(1, "0000:05:00.0", "amdgpu", "0x1002", "0x1234", "0x120000"); // an accelerator, not display
        sys.card(2, "0000:00:02.0", "i915", "0x8086", "0x9a49");
        GpuSampler sampler(sys.path(), {}, noNvml);
        const GpuMetrics metrics = sampler.sample(1.0, AutoGpu);
        QCOMPARE(metrics.devices.size(), std::size_t(1));
        QCOMPARE(metrics.devices.front().driver, std::string("i915"));
    }

    void namesFromPciIds()
    {
        QTemporaryDir dir;
        QFile ids(dir.filePath(QStringLiteral("pci.ids")));
        QVERIFY(ids.open(QIODevice::WriteOnly));
        ids.write("# pci.ids\n"
                  "1002  Advanced Micro Devices, Inc. [AMD/ATI]\n"
                  "\t73bf  Navi 21 [Radeon RX 6800/6800 XT / 6900 XT]\n"
                  "8086  Intel Corporation\n"
                  "\t9a40  Other\n"
                  "\t9a49  TigerLake-LP GT2 [Iris Xe Graphics]\n"
                  "\t\t1028 0a1f  Subsystem name that must be ignored\n"
                  "8087  Intel Corporation\n"
                  "\t9a49  Wrong vendor\n");
        ids.close();
        PciNames names({dir.filePath(QStringLiteral("missing")).toStdString(), ids.fileName().toStdString()});
        QCOMPARE(names.device(0x8086, 0x9a49), std::string("TigerLake-LP GT2 [Iris Xe Graphics]"));
        QCOMPARE(names.device(0x1002, 0x73bf), std::string("Navi 21 [Radeon RX 6800/6800 XT / 6900 XT]"));
        QCOMPARE(names.device(0x8086, 0x1111), std::string());
        QCOMPARE(names.device(0xffff, 0x0001), std::string());

        FakeSys sys;
        sys.card(1, "0000:00:02.0", "i915", "0x8086", "0x9a49");
        GpuSampler sampler(sys.path(), {ids.fileName().toStdString()}, noNvml);
        QCOMPARE(sampler.sample(1.0, AutoGpu).devices.front().model,
                 std::string("Intel TigerLake-LP GT2 [Iris Xe Graphics]"));
    }
};

QTEST_GUILESS_MAIN(TestGpu)
#include "tst_gpu.moc"
