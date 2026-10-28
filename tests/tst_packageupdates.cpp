#include "metrics/packageupdates.h"

#include <QTest>

using namespace aqtruncat::metrics;

// F5.4 / F5.5: manager choice, read-only commands and output counting.
class TestPackageUpdates : public QObject
{
    Q_OBJECT

private slots:
    void choosesByWhatIsInstalledThenByDistribution()
    {
        PackageManagerAvailability onlyApt{false, true, false};
        QCOMPARE(choosePackageManager(onlyApt, "arch", {}), PackageManager::Apt); // capabilities beat names
        PackageManagerAvailability all{true, true, true};
        QCOMPARE(choosePackageManager(all, "fedora", {}), PackageManager::Dnf);
        QCOMPARE(choosePackageManager(all, "linuxmint", {"ubuntu", "debian"}), PackageManager::Apt);
        QCOMPARE(choosePackageManager(all, "zorin", {"ubuntu"}), PackageManager::Apt);
        QCOMPARE(choosePackageManager(all, "garuda", {"arch"}), PackageManager::Pacman);
        QCOMPARE(choosePackageManager(all, "unknown", {}), PackageManager::Pacman); // fixed order
        PackageManagerAvailability none;
        QCOMPARE(choosePackageManager(none, "arch", {}), PackageManager::None);
    }

    void commandsOnlyRead()
    {
        std::string reason;
        auto spec = packageUpdateCommand(PackageManager::Pacman, false, true, &reason);
        QCOMPARE(spec->program, std::string("checkupdates"));
        QCOMPARE(spec->arguments, (std::vector<std::string>{"--nosync", "--nocolor"}));
        QVERIFY(reason.empty());
        spec = packageUpdateCommand(PackageManager::Pacman, true, true, &reason);
        QCOMPARE(spec->program, std::string("checkupdates"));
        QCOMPARE(spec->arguments, (std::vector<std::string>{"--nocolor"}));
        reason.clear();
        spec = packageUpdateCommand(PackageManager::Pacman, true, false, &reason);
        QVERIFY(!spec);
        QVERIFY(QString::fromStdString(reason).contains(QLatin1String("pacman-contrib")));
        QVERIFY(QString::fromStdString(reason).contains(QLatin1String("sudo pacman -S pacman-contrib")));

        reason.clear();
        spec = packageUpdateCommand(PackageManager::Apt, true, false, &reason);
        QCOMPARE(spec->program, std::string("apt-get"));
        QCOMPARE(spec->arguments, (std::vector<std::string>{"-s", "-q", "-o", "Debug::NoLocking=true", "dist-upgrade"}));
        QVERIFY(QString::fromStdString(reason).contains(QLatin1String("root"))); // apt cannot refresh

        spec = packageUpdateCommand(PackageManager::Dnf, false, false);
        QCOMPARE(spec->arguments, (std::vector<std::string>{"-q", "-C", "check-update"}));
        spec = packageUpdateCommand(PackageManager::Dnf, true, false);
        QCOMPARE(spec->arguments, (std::vector<std::string>{"-q", "--refresh", "check-update"}));

        reason.clear();
        QVERIFY(!packageUpdateCommand(PackageManager::None, false, false, &reason));
        QVERIFY(!reason.empty());

        // Never an install, upgrade or removal verb, never elevation.
        for (const PackageManager manager : {PackageManager::Pacman, PackageManager::Apt, PackageManager::Dnf}) {
            for (const bool remote : {false, true}) {
                const auto command = packageUpdateCommand(manager, remote, true);
                QVERIFY(command->program != "sudo" && command->program != "pkexec");
                for (const std::string &argument : command->arguments) {
                    QVERIFY2(argument != "-S" && argument != "-Syu" && argument != "-y" && argument != "install"
                                 && argument != "upgrade" && argument != "remove" && argument != "update",
                             argument.c_str());
                }
                if (manager == PackageManager::Apt)
                    QVERIFY(command->arguments.front() == "-s"); // simulation only
            }
        }
    }

    void checkupdatesCountsRecordsAndExitStatuses()
    {
        const char out[] = "linux 6.17.1.arch1-1 -> 6.17.2.arch1-1\n"
                           "mesa 1:25.2.4-1 -> 1:25.2.5-1\n"
                           "firefox 143.0-1 -> 144.0-1 [ignored]\n";
        UpdateCount count = parseCheckupdates(out, 0, "warning: nonfatal diagnostic\n");
        QCOMPARE(*count.count, 2);
        QCOMPARE(count.ignored, 1);
        QCOMPARE(*parseCheckupdates("a 1-1 -> 2-1\n\nb 3-1 -> 4-1\n", 0, "").count, 2);
        QCOMPARE(*parseCheckupdates("", 2, "").count, 0);
        QCOMPARE(*parseCheckupdates("\n  \r\n", 2, "warning only\n").count, 0);
        QVERIFY(!parseCheckupdates("a 1-1 -> 2-1\n", 2, "").count);
        QVERIFY(!parseCheckupdates("", 0, "").count);
        QVERIFY(!parseCheckupdates("", 1, "==> ERROR: Cannot fetch updates\n").count);
        QVERIFY(!parseCheckupdates("warning text\na 1-1 -> 2-1\n", 0, "").count);
        QVERIFY(!parseCheckupdates("", 3, "error\n").count);
    }

    void aptCountsUpgradesNotNewPackages()
    {
        const char out[] = "Reading package lists...\n"
                           "Inst passwd [1:4.15.3-3ubuntu2] (1:4.16.0-7ubuntu1 Ubuntu:25.04/plucky [amd64])\n"
                           "Inst procps [2:4.0.4-4ubuntu5] (2:4.0.4-7ubuntu1 Ubuntu:25.04/plucky [amd64])\n"
                           "Inst linux-sysctl-defaults (4.10.1ubuntu2 Ubuntu:25.04/plucky [all])\n"
                           "Conf passwd (1:4.16.0-7ubuntu1 Ubuntu:25.04/plucky [amd64])\n"
                           "Inst passwd [1:4.15.3-3ubuntu2] (duplicate line is one package)\n";
        QCOMPARE(*parseAptSimulation(out, 0, "").count, 2);
        QCOMPARE(*parseAptSimulation("0 upgraded, 0 newly installed\n", 0, "").count, 0);
        const UpdateCount failed = parseAptSimulation("", 100, "E: Could not open lock file\n");
        QVERIFY(!failed.count);
        QVERIFY(!failed.error.empty());
    }

    void dnfCountsBothVersions()
    {
        // dnf5
        const char dnf5[] = "Upgrades\n"
                            "ca-certificates.noarch             2026.2.92_v9.0.319-1.fc44 updates\n"
                            "curl.x86_64                        8.18.0-14.fc44            updates\n"
                            "dnf5.x86_64                        5.4.6.0-1.fc44            updates\n";
        QCOMPARE(*parseDnfCheckUpdate(dnf5, 100, "").count, 3);
        // dnf4, with an epoch and an obsoletes section that repeats a package
        const char dnf4[] = "Last metadata expiration check: 0:10:02 ago on Wed 22 Oct 2026.\n"
                            "\n"
                            "kernel.x86_64          6.17.2-200.fc43       updates\n"
                            "vim-minimal.x86_64     2:9.1.1000-1.fc43     updates\n"
                            "Obsoleting Packages\n"
                            "grub2-tools.x86_64     1:2.12-10.fc43        updates\n"
                            "    grub2-tools.x86_64 1:2.12-9.fc43         @fedora\n";
        QCOMPARE(*parseDnfCheckUpdate(dnf4, 100, "").count, 2);
        QCOMPARE(*parseDnfCheckUpdate("", 0, "").count, 0);
        QVERIFY(!parseDnfCheckUpdate("", 1, "Cache-only enabled but no cache for 'fedora'\n").count);
    }

    void flatpakRefs()
    {
        const char out[] = "app/io.freetubeapp.FreeTube/x86_64/stable\n"
                           "org.freedesktop.Platform/x86_64/25.08\n"
                           "org.freedesktop.Platform/x86_64/25.08\n"
                           "\n";
        QCOMPARE(*parseFlatpakRefs(out, 0, "").count, 2);
        QCOMPARE(*parseFlatpakRefs("", 0, "").count, 0);
        const UpdateCount failed = parseFlatpakRefs("", 1, "error: Unable to load summary from remote flathub: No cached summary\n");
        QVERIFY(!failed.count);
        QVERIFY(QString::fromStdString(failed.error).contains(QLatin1String("No cached summary")));
    }

    void flatpakInstallationsConfiguration()
    {
        const auto names = parseFlatpakInstallations("[Installation \"extra\"]\n"
                                                     "Path=/var/lib/flatpak-extra\n"
                                                     "[Installation \"steam-deck_1\"]\n"
                                                     "[Installation \"bad name;rm\"]\n"
                                                     "[Installation \"extra\"]\n");
        QCOMPARE(names, (std::vector<std::string>{"extra", "steam-deck_1"}));
    }

    void failureTextIsBoundedAndClean()
    {
        QCOMPARE(commandFailure("\n  first line\nsecond\n", 1), std::string("first line"));
        QCOMPARE(commandFailure("", 7), std::string("the check failed (exit status 7)"));
        QCOMPARE(commandFailure(std::string(500, 'x'), 1).size(), std::size_t(200));
        QCOMPARE(commandFailure("bad\x1b[31mcolour", 1), std::string("bad [31mcolour"));
    }
};

QTEST_GUILESS_MAIN(TestPackageUpdates)
#include "tst_packageupdates.moc"
