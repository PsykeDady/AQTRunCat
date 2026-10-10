#include "metrics/network.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace aqtruncat::metrics;

namespace {

std::string netDev(const std::vector<std::tuple<std::string, std::uint64_t, std::uint64_t>> &rows)
{
    std::string text =
        "Inter-|   Receive                                                |  Transmit\n"
        " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n";
    for (const auto &[name, rx, tx] : rows)
        text += "  " + name + ": " + std::to_string(rx) + " 10 0 0 0 0 0 0 " + std::to_string(tx) + " 20 0 0 0 0 0 0\n";
    return text;
}

// /sys/class/net look-alike: physical interfaces get a "device" entry.
struct SysClassNet {
    QTemporaryDir tmp;
    std::filesystem::path root{tmp.path().toStdString()};

    void add(const std::string &name, bool physical)
    {
        const QDir dir(QString::fromStdString((root / name).string()));
        dir.mkpath(QStringLiteral("."));
        if (physical) {
            QFile device(dir.filePath(QStringLiteral("device")));
            if (!device.open(QIODevice::WriteOnly))
                qFatal("cannot create test fixture %s", qPrintable(device.fileName()));
        }
    }
};

// Tracker fed with explicit counters and times; counts physical interfaces.
struct NetTracker {
    SysClassNet sys;
    CounterRateTracker tracker;
    NetworkSelection selection;

    void select(NetworkSelection next)
    {
        if (next == selection)
            return;
        selection = std::move(next);
        tracker.reset();
    }

    AggregateRates update(const std::string &procNetDev, double time)
    {
        const auto counters = parseNetDev(procNetDev);
        return tracker.update(*counters, time, [this](const std::string &name) {
            if (selection.mode == NetworkPolicyMode::Manual)
                return selection.interfaces.contains(name);
            return countsTowardsNetworkTotal(classifyInterface(name, sys.root));
        });
    }
};

const SourceRate *find(const AggregateRates &rates, const std::string &name)
{
    for (const auto &s : rates.sources)
        if (s.name == name)
            return &s;
    return nullptr;
}

} // namespace

class TestNetwork : public QObject
{
    Q_OBJECT

private slots:
    void parsesProcNetDev()
    {
        const auto c = parseNetDev(netDev({{"lo", 3552802, 3552802}, {"wlan0", 5075793998ull, 1526975316ull}, {"docker0", 0, 0}}));
        QVERIFY(c);
        QCOMPARE(c->size(), 3u);
        QCOMPARE(c->at("wlan0").first, 5075793998ull);
        QCOMPARE(c->at("wlan0").second, 1526975316ull);
        QCOMPARE(c->at("lo").first, 3552802ull);
    }

    void parsesNamesWithoutSpaceAfterColon()
    {
        // Older kernels print "eth0:123 …" when the number is wide.
        const auto c = parseNetDev("hdr\nhdr\n  eth0:123 1 0 0 0 0 0 0 456 2 0 0 0 0 0 0\n");
        QVERIFY(c);
        QCOMPARE(c->at("eth0").first, 123ull);
        QCOMPARE(c->at("eth0").second, 456ull);
    }

    void rejectsMalformedLines()
    {
        std::string error;
        QVERIFY(!parseNetDev("hdr\nhdr\n  eth0: 1 2 3\n", &error));
        QVERIFY(!parseNetDev("hdr\nhdr\n  eth0: x 1 0 0 0 0 0 0 456 2 0 0 0 0 0 0\n", &error));
        QVERIFY(!error.empty());
        QVERIFY(parseNetDev("hdr\nhdr\n")->empty()); // no interfaces is valid
    }

    void classifiesWithoutAssumingNames()
    {
        SysClassNet sys;
        sys.add("enp3s0", true);
        sys.add("wlp2s0", true);
        sys.add("tun0", false);
        sys.add("wg0", false);
        sys.add("docker0", false);
        sys.add("veth12ab", false);
        sys.add("br-lan", false);
        QCOMPARE(classifyInterface("lo", sys.root), InterfaceKind::Loopback);
        QCOMPARE(classifyInterface("enp3s0", sys.root), InterfaceKind::Physical);
        QCOMPARE(classifyInterface("wlp2s0", sys.root), InterfaceKind::Physical);
        for (const char *name : {"tun0", "wg0", "docker0", "veth12ab", "br-lan", "gone0"})
            QCOMPARE(classifyInterface(name, sys.root), InterfaceKind::Virtual);
        QVERIFY(countsTowardsNetworkTotal(InterfaceKind::Physical));
        QVERIFY(!countsTowardsNetworkTotal(InterfaceKind::Loopback));
        QVERIFY(!countsTowardsNetworkTotal(InterfaceKind::Virtual));
    }

    void firstSampleIsPending()
    {
        NetTracker t;
        t.sys.add("eth0", true);
        const auto r = t.update(netDev({{"eth0", 1000, 2000}}), 10.0);
        QCOMPARE(r.status.state, MetricState::Pending);
        QVERIFY(!r.firstPerSecond); // not 0: unknown
    }

    void rateUsesActualElapsedTime()
    {
        NetTracker t;
        t.sys.add("eth0", true);
        t.update(netDev({{"eth0", 1000, 2000}}), 10.0);
        // 2.5 s really elapsed (not the nominal 1 s interval).
        const auto r = t.update(netDev({{"eth0", 1000 + 5000, 2000 + 250}}), 12.5);
        QVERIFY(r.status.ok());
        QCOMPARE(*r.firstPerSecond, 2000.0); // 5000 B / 2.5 s
        QCOMPARE(*r.secondPerSecond, 100.0);
    }

    void longGapAveragesInsteadOfSpiking()
    {
        // A stalled process (SIGSTOP, heavy load) sees a long monotonic gap;
        // across system suspend CLOCK_MONOTONIC stops, so the gap is short and
        // the counters barely moved. Either way the rate is an average over
        // the real elapsed time, never a spike (M8).
        NetTracker t;
        t.sys.add("eth0", true);
        t.update(netDev({{"eth0", 1000, 2000}}), 100.0);
        const auto r = t.update(netDev({{"eth0", 1000 + 36000, 2000 + 3600}}), 100.0 + 3600.0);
        QVERIFY(r.status.ok());
        QCOMPARE(*r.firstPerSecond, 10.0);
        QCOMPARE(*r.secondPerSecond, 1.0);
        // The next normal interval is measured on its own.
        const auto next = t.update(netDev({{"eth0", 1000 + 36000 + 500, 2000 + 3600}}), 100.0 + 3601.0);
        QCOMPARE(*next.firstPerSecond, 500.0);
    }

    void zeroDeltaIsZeroNotUnavailable()
    {
        NetTracker t;
        t.sys.add("eth0", true);
        t.update(netDev({{"eth0", 1000, 2000}}), 1.0);
        const auto r = t.update(netDev({{"eth0", 1000, 2000}}), 2.0);
        QVERIFY(r.status.ok());
        QCOMPARE(*r.firstPerSecond, 0.0);
    }

    void zeroOrInvalidElapsedKeepsBaseline()
    {
        NetTracker t;
        t.sys.add("eth0", true);
        t.update(netDev({{"eth0", 1000, 0}}), 5.0);
        QCOMPARE(t.update(netDev({{"eth0", 2000, 0}}), 5.0).status.state, MetricState::Pending);  // same time
        QCOMPARE(t.update(netDev({{"eth0", 3000, 0}}), 4.0).status.state, MetricState::Pending);  // clock went back
        QCOMPARE(t.update(netDev({{"eth0", 3000, 0}}), qQNaN()).status.state, MetricState::Pending);
        const auto r = t.update(netDev({{"eth0", 4000, 0}}), 6.0); // measured from the t=5 baseline
        QVERIFY(r.status.ok());
        QCOMPARE(*r.firstPerSecond, 3000.0);
    }

    void counterResetNeverGivesHugeRates()
    {
        NetTracker t;
        t.sys.add("eth0", true);
        t.sys.add("wlan0", true);
        t.update(netDev({{"eth0", 5000000000ull, 100}, {"wlan0", 1000, 1000}}), 1.0);
        // eth0 was re-created (driver reload): counters restart from small values.
        const auto r = t.update(netDev({{"eth0", 10, 5}, {"wlan0", 3000, 1500}}), 2.0);
        QVERIFY(r.status.ok());
        QVERIFY(find(r, "eth0")->counterReset);
        QVERIFY(!find(r, "eth0")->firstPerSecond);
        QCOMPARE(*r.firstPerSecond, 2000.0); // only wlan0
        QVERIFY(*r.firstPerSecond < 1e12);
        // Next interval eth0 is measured from its new baseline.
        const auto next = t.update(netDev({{"eth0", 110, 5}, {"wlan0", 3000, 1500}}), 3.0);
        QCOMPARE(*find(next, "eth0")->firstPerSecond, 100.0);
    }

    void onlyResetSourcesIsPending()
    {
        NetTracker t;
        t.sys.add("eth0", true);
        t.update(netDev({{"eth0", 5000, 5000}}), 1.0);
        QCOMPARE(t.update(netDev({{"eth0", 1, 1}}), 2.0).status.state, MetricState::Pending);
    }

    void interfacesAppearingAndDisappearing()
    {
        NetTracker t;
        t.sys.add("eth0", true);
        t.sys.add("usb0", true);
        t.update(netDev({{"eth0", 0, 0}}), 1.0);
        // usb0 appears (tethering plugged in): no rate for it yet.
        auto r = t.update(netDev({{"eth0", 100, 0}, {"usb0", 999999, 0}}), 2.0);
        QCOMPARE(*r.firstPerSecond, 100.0);
        QVERIFY(!find(r, "usb0")->firstPerSecond);
        QCOMPARE(r.countedSources, 2);
        // eth0 disappears: it is simply absent, usb0 now has a rate.
        r = t.update(netDev({{"usb0", 1999999, 0}}), 3.0);
        QVERIFY(!find(r, "eth0"));
        QCOMPARE(*r.firstPerSecond, 1000000.0);
    }

    void aggregateExcludesLoopbackAndVirtual()
    {
        NetTracker t;
        t.sys.add("enp3s0", true);
        t.sys.add("wlp2s0", true);
        t.sys.add("tun0", false);   // VPN: its traffic also crosses a physical interface
        t.sys.add("docker0", false);
        t.update(netDev({{"lo", 0, 0}, {"enp3s0", 0, 0}, {"wlp2s0", 0, 0}, {"tun0", 0, 0}, {"docker0", 0, 0}}), 1.0);
        const auto r = t.update(netDev({{"lo", 1000000, 1000000}, {"enp3s0", 100, 10}, {"wlp2s0", 200, 20},
                                        {"tun0", 250, 25}, {"docker0", 5000, 5000}}), 2.0);
        QVERIFY(r.status.ok());
        QCOMPARE(*r.firstPerSecond, 300.0);  // enp3s0 + wlp2s0
        QCOMPARE(*r.secondPerSecond, 30.0);
        QCOMPARE(r.countedSources, 2);
        // Per-interface rates are kept for every interface (future selection UI).
        QCOMPARE(*find(r, "tun0")->firstPerSecond, 250.0);
        QVERIFY(!find(r, "tun0")->counted);
        QVERIFY(!find(r, "lo")->counted);
    }

    void onlyVirtualInterfacesIsUnavailable()
    {
        NetTracker t;
        t.sys.add("docker0", false);
        t.update(netDev({{"lo", 0, 0}, {"docker0", 0, 0}}), 1.0);
        QCOMPARE(t.update(netDev({{"lo", 10, 10}, {"docker0", 0, 0}}), 2.0).status.state, MetricState::Unavailable);
    }

    void manualCountsExactlySelectedIncludingVirtual()
    {
        NetTracker t;
        t.sys.add("wlan0", true);
        t.sys.add("tun0", false);
        t.select({NetworkPolicyMode::Manual, {"tun0"}});
        t.update(netDev({{"wlan0", 0, 0}, {"tun0", 100, 200}}), 1.0);
        const auto r = t.update(netDev({{"wlan0", 9000, 9000}, {"tun0", 500, 800}}), 2.0);
        QVERIFY(r.status.ok());
        QCOMPARE(*r.firstPerSecond, 400.0);
        QCOMPARE(*r.secondPerSecond, 600.0);
        QVERIFY(find(r, "tun0")->counted);
        QVERIFY(!find(r, "wlan0")->counted);
    }

    void manualZeroTrafficIsARealZero()
    {
        NetTracker t;
        t.select({NetworkPolicyMode::Manual, {"tun0"}});
        t.update(netDev({{"tun0", 100, 200}}), 1.0);
        const auto r = t.update(netDev({{"tun0", 100, 200}}), 2.0);
        QVERIFY(r.status.ok());
        QCOMPARE(*r.firstPerSecond, 0.0);
        QCOMPARE(*r.secondPerSecond, 0.0);
    }

    void selectedInterfaceMayDisappearAndReturn()
    {
        NetTracker t;
        t.select({NetworkPolicyMode::Manual, {"tun0"}});
        t.update(netDev({{"tun0", 100, 100}}), 1.0);
        QCOMPARE(t.update(netDev({}), 2.0).status.state, MetricState::Unavailable);
        QCOMPARE(t.update(netDev({{"tun0", 1000, 1000}}), 3.0).status.state, MetricState::Pending);
        const auto back = t.update(netDev({{"tun0", 1300, 1200}}), 4.0);
        QVERIFY(back.status.ok());
        QCOMPARE(*back.firstPerSecond, 300.0);
    }

    void policyChangeStartsNewBaselineWithoutSpike()
    {
        NetTracker t;
        t.sys.add("wlan0", true);
        t.update(netDev({{"wlan0", 100, 100}, {"tun0", 1000000000, 1000000000}}), 1.0);
        QVERIFY(t.update(netDev({{"wlan0", 200, 200}, {"tun0", 2000000000, 2000000000}}), 2.0).status.ok());

        t.select({NetworkPolicyMode::Manual, {"tun0"}});
        QCOMPARE(t.update(netDev({{"wlan0", 300, 300}, {"tun0", 3000000000, 3000000000}}), 3.0).status.state,
                 MetricState::Pending);
        const auto manual = t.update(netDev({{"wlan0", 400, 400}, {"tun0", 3000000250, 3000000050}}), 4.0);
        QVERIFY(manual.status.ok());
        QCOMPARE(*manual.firstPerSecond, 250.0);

        t.select({});
        QCOMPARE(t.update(netDev({{"wlan0", 500, 500}, {"tun0", 4000000000, 4000000000}}), 5.0).status.state,
                 MetricState::Pending);
        const auto automatic = t.update(netDev({{"wlan0", 600, 550}, {"tun0", 5000000000, 5000000000}}), 6.0);
        QVERIFY(automatic.status.ok());
        QCOMPARE(*automatic.firstPerSecond, 100.0);
        QCOMPARE(*automatic.secondPerSecond, 50.0);
    }
};

QTEST_GUILESS_MAIN(TestNetwork)
#include "tst_network.moc"
