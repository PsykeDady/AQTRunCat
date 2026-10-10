#include "metrics/metricsformat.h"

#include <QTest>

#include <clocale>
#include <limits>

using namespace aqtruncat::metrics;

class TestMetricsFormat : public QObject
{
    Q_OBJECT

private slots:
    void bytes_data()
    {
        QTest::addColumn<quint64>("bytes");
        QTest::addColumn<QString>("text");
        QTest::newRow("zero") << quint64(0) << QStringLiteral("0 B");
        QTest::newRow("1023") << quint64(1023) << QStringLiteral("1023 B");
        QTest::newRow("1024") << quint64(1024) << QStringLiteral("1.0 KiB");
        QTest::newRow("1536") << quint64(1536) << QStringLiteral("1.5 KiB");
        QTest::newRow("just below 1 MiB") << quint64(1048575) << QStringLiteral("1.0 MiB"); // not "1024.0 KiB"
        QTest::newRow("1 MiB") << quint64(1048576) << QStringLiteral("1.0 MiB");
        QTest::newRow("6.2 GiB") << quint64(6657199308ull) << QStringLiteral("6.2 GiB");
        QTest::newRow("1 TiB") << quint64(1ull << 40) << QStringLiteral("1.0 TiB");
        QTest::newRow("max") << quint64(std::numeric_limits<quint64>::max()) << QStringLiteral("16384.0 PiB");
    }

    void bytes()
    {
        QFETCH(quint64, bytes);
        QFETCH(QString, text);
        QCOMPARE(QString::fromStdString(formatBytes(bytes)), text);
    }

    void rates()
    {
        QCOMPARE(QString::fromStdString(formatRate(0.0)), QStringLiteral("0 B/s"));
        QCOMPARE(QString::fromStdString(formatRate(0.4)), QStringLiteral("0 B/s"));
        QCOMPARE(QString::fromStdString(formatRate(1023.4)), QStringLiteral("1023 B/s"));
        QCOMPARE(QString::fromStdString(formatRate(1023.6)), QStringLiteral("1.0 KiB/s"));
        QCOMPARE(QString::fromStdString(formatRate(84275.2)), QStringLiteral("82.3 KiB/s"));
        QCOMPARE(QString::fromStdString(formatRate(1468006.4)), QStringLiteral("1.4 MiB/s"));
        QCOMPARE(QString::fromStdString(formatRate(-1.0)), QString::fromLatin1(UnavailableText));
        QCOMPARE(QString::fromStdString(formatRate(std::numeric_limits<double>::quiet_NaN())), QString::fromLatin1(UnavailableText));
    }

    void percent()
    {
        QCOMPARE(QString::fromStdString(formatPercent(0.0)), QStringLiteral("0.0 %"));
        QCOMPARE(QString::fromStdString(formatPercent(23.44)), QStringLiteral("23.4 %"));
        QCOMPARE(QString::fromStdString(formatPercent(99.96)), QStringLiteral("100.0 %"));
        QCOMPARE(QString::fromStdString(formatPercent(100.0)), QStringLiteral("100.0 %"));
        QCOMPARE(QString::fromStdString(formatPercent(std::numeric_limits<double>::infinity())), QString::fromLatin1(UnavailableText));
    }

    void independentOfTheProcessLocale()
    {
        // An Italian/German locale uses ',' as decimal separator for printf.
        const char *previous = std::setlocale(LC_NUMERIC, nullptr);
        const std::string saved = previous ? previous : "C";
        if (!std::setlocale(LC_NUMERIC, "it_IT.UTF-8") && !std::setlocale(LC_NUMERIC, "de_DE.UTF-8"))
            QSKIP("no comma-decimal locale installed");
        const std::string text = formatPercent(23.44) + " " + formatBytes(1536);
        std::setlocale(LC_NUMERIC, saved.c_str());
        QCOMPARE(QString::fromStdString(text), QStringLiteral("23.4 % 1.5 KiB"));
    }
};

QTEST_APPLESS_MAIN(TestMetricsFormat)
#include "tst_metricsformat.moc"
