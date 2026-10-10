#include "app/storagepaths.h"

#include <QTest>

#include <map>

class TestStoragePaths : public QObject
{
    Q_OBJECT

private slots:
    void rootAndHomeOnSameFilesystem()
    {
        const auto paths = desktopStoragePaths("/home/alice", [](const std::string &) {
            return std::optional<std::uint64_t>(42);
        });
        QCOMPARE(paths, std::vector<std::string>{"/"});
    }

    void rootAndHomeOnDifferentFilesystems()
    {
        const auto paths = desktopStoragePaths("/home/alice", [](const std::string &path) {
            return std::optional<std::uint64_t>(path == "/" ? 42 : 84);
        });
        QCOMPARE(paths, (std::vector<std::string>{"/", "/home/alice"}));
    }

    void unresolvedHomeIsNotDuplicated()
    {
        const auto paths = desktopStoragePaths("/missing", [](const std::string &path) -> std::optional<std::uint64_t> {
            return path == "/" ? std::optional<std::uint64_t>(42) : std::nullopt;
        });
        QCOMPARE(paths, std::vector<std::string>{"/"});
    }
};

QTEST_APPLESS_MAIN(TestStoragePaths)
#include "tst_storagepaths.moc"
