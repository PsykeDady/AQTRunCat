#include "metrics/compactformat.h"

#include <QTest>

#include <clocale>
#include <limits>

using aqtruncat::metrics::compactPercent;
using aqtruncat::metrics::compactRate;

namespace {

QString text(const std::optional<std::string> &value)
{
    return value ? QString::fromStdString(*value) : QStringLiteral("<none>");
}

} // namespace

class TestCompactFormat : public QObject
{
    Q_OBJECT

private slots:
    void percent_data()
    {
        QTest::addColumn<double>("value");
        QTest::addColumn<QString>("expected");
        QTest::newRow("zero") << 0.0 << "0%";
        QTest::newRow("below half") << 0.49 << "0%";
        QTest::newRow("half rounds up") << 0.5 << "1%";
        QTest::newRow("typical") << 23.4 << "23%";
        QTest::newRow("rounds up") << 23.6 << "24%";
        QTest::newRow("almost full") << 99.4 << "99%";
        QTest::newRow("rounds to full") << 99.5 << "100%";
        QTest::newRow("full") << 100.0 << "100%";
        QTest::newRow("clamped high") << 140.0 << "100%";
        QTest::newRow("clamped low") << -3.0 << "0%";
    }

    void percent()
    {
        QFETCH(double, value);
        QFETCH(QString, expected);
        QCOMPARE(text(compactPercent(value)), expected);
        QVERIFY(text(compactPercent(value)).size() <= 4);
    }

    void percentWithoutValue()
    {
        QVERIFY(!compactPercent(std::numeric_limits<double>::quiet_NaN()));
        QVERIFY(!compactPercent(std::numeric_limits<double>::infinity()));
    }

    void rate_data()
    {
        QTest::addColumn<double>("value");
        QTest::addColumn<QString>("expected");
        QTest::newRow("zero") << 0.0 << "0B";
        QTest::newRow("fraction") << 0.4 << "0B";
        QTest::newRow("bytes") << 850.0 << "850B";
        QTest::newRow("last byte value") << 999.4 << "999B";
        QTest::newRow("rounds into K") << 999.5 << "1.0K";
        QTest::newRow("one KiB") << 1024.0 << "1.0K";
        QTest::newRow("1.2 KiB") << 1.2 * 1024 << "1.2K";
        QTest::newRow("9.94 KiB") << 9.94 * 1024 << "9.9K";
        QTest::newRow("9.95 KiB is 10K") << 9.95 * 1024 << "10K";
        QTest::newRow("850 KiB") << 850.0 * 1024 << "850K";
        QTest::newRow("999.4 KiB") << 999.4 * 1024 << "999K";
        QTest::newRow("999.5 KiB is 1.0M") << 999.5 * 1024 << "1.0M";
        QTest::newRow("1.24 MiB") << 1.24 * 1024 * 1024 << "1.2M";
        QTest::newRow("32 MiB") << 32.0 * 1024 * 1024 << "32M";
        QTest::newRow("1 GiB") << 1024.0 * 1024 * 1024 << "1.0G";
        QTest::newRow("3 TiB") << 3.0 * 1024 * 1024 * 1024 * 1024 << "3.0T";
        QTest::newRow("saturates") << 1e30 << ">999E";
    }

    void rate()
    {
        QFETCH(double, value);
        QFETCH(QString, expected);
        QCOMPARE(text(compactRate(value)), expected);
    }

    void rateIsAtMostFourCharactersInRange()
    {
        for (double value = 0.0; value < 1e18; value = value * 1.37 + 1.0)
            QVERIFY2(text(compactRate(value)).size() <= 4, qPrintable(text(compactRate(value))));
    }

    void rateWithoutValue()
    {
        QVERIFY(!compactRate(-1.0));
        QVERIFY(!compactRate(std::numeric_limits<double>::quiet_NaN()));
        QVERIFY(!compactRate(std::numeric_limits<double>::infinity()));
    }

    void independentOfProcessLocale()
    {
        // QCoreApplication adopts the system locale; Italian uses ','.
        const char *previous = std::setlocale(LC_ALL, nullptr);
        const std::string saved = previous ? previous : "C";
        if (!std::setlocale(LC_ALL, "it_IT.UTF-8") && !std::setlocale(LC_ALL, "de_DE.UTF-8"))
            QSKIP("no comma-decimal locale installed");
        const QString rate = text(compactRate(1.2 * 1024 * 1024));
        std::setlocale(LC_ALL, saved.c_str());
        QCOMPARE(rate, QStringLiteral("1.2M"));
    }
};

QTEST_APPLESS_MAIN(TestCompactFormat)
#include "tst_compactformat.moc"
