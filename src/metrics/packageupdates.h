#pragma once

// F5.4 / F5.5 available updates: the model of the two metrics and the
// pure, testable parts of the checks (which package manager, which
// read-only command, how its output is counted). Running the commands is
// the backend's job (app/updatesservice), with bounded subprocesses.
//
// Every command here only reads: no install, upgrade or removal, no root.
// By default only locally cached metadata is used; a remote refresh is a
// separate, opt-in command that still never touches the system databases.

#include "metricstatus.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aqtruncat::metrics {

enum class PackageManager { None, Pacman, Apt, Dnf };

const char *packageManagerName(PackageManager manager); // "pacman", "apt", "dnf", ""

struct FlatpakInstallationCount {
    std::string name; // "user", "system", or a configured installation name
    MetricStatus status;
    int apps = 0;
    int runtimes = 0;
};

struct UpdatesMetrics {
    MetricStatus status;
    std::optional<int> count;   // available updates; only when status is Ok
    std::string manager;        // "pacman", "apt", "dnf" or "flatpak"
    bool checking = false;      // a check is running now
    std::optional<std::int64_t> lastSuccess;  // epoch seconds of the last successful check
    std::optional<std::int64_t> metadataTime; // epoch seconds of the package metadata used
    bool metadataStale = false; // older than StaleMetadataSeconds
    bool remote = false;        // the last check refreshed metadata remotely
    std::string note;           // e.g. why a remote refresh is not possible
    int ignored = 0;            // pacman: updates of IgnorePkg packages, not counted
    int apps = 0;               // flatpak: count = apps + runtimes
    int runtimes = 0;
    std::vector<FlatpakInstallationCount> installations;
};

// Package metadata older than a week is reported as stale.
inline constexpr std::int64_t StaleMetadataSeconds = 7 * 24 * 3600;

struct PackageManagerAvailability {
    bool pacman = false;
    bool apt = false;
    bool dnf = false;
};

// The manager to use: the one that matches the distribution (ID, then
// ID_LIKE: arch → pacman, debian/ubuntu → apt, fedora/rhel/centos → dnf)
// among those actually present, else the first present in that order.
PackageManager choosePackageManager(const PackageManagerAvailability &available, const std::string &id,
                                    const std::vector<std::string> &idLike);

struct CommandSpec {
    std::string program;                // looked up in PATH by the runner
    std::vector<std::string> arguments;
};

// The read-only check. remote: refresh metadata first where that is
// possible without root (pacman needs checkupdates from pacman-contrib;
// dnf refreshes the user's own cache; apt cannot: nullopt + reason).
std::optional<CommandSpec> packageUpdateCommand(PackageManager manager, bool remote, bool checkupdatesAvailable,
                                                std::string *reason = nullptr);

struct UpdateCount {
    std::optional<int> count;
    int ignored = 0;
    int apps = 0;
    int runtimes = 0;
    std::string error;
};

// pacman -Qu: one "name old -> new" line per update; " [ignored]" lines
// are not counted. Exit 1 with no output means no updates.
UpdateCount parsePacmanQu(std::string_view out, int exitCode, std::string_view err);
// checkupdates: same lines; exit 2 = no updates.
UpdateCount parseCheckupdates(std::string_view out, int exitCode, std::string_view err);
// apt-get -s dist-upgrade: "Inst name [old] (new …)" = an upgrade of an
// installed package; "Inst name (…)" without [old] is a new dependency.
UpdateCount parseAptSimulation(std::string_view out, int exitCode, std::string_view err);
// dnf check-update (dnf4 and dnf5): exit 100 = updates listed as
// "name.arch version repo"; 0 = none; obsoletes are not counted twice.
UpdateCount parseDnfCheckUpdate(std::string_view out, int exitCode, std::string_view err);
// flatpak remote-ls --updates --columns=ref: one "id/arch/branch" per line.
UpdateCount parseFlatpakRefs(std::string_view out, int exitCode, std::string_view err);

// Installation names from /etc/flatpak/installations.d/*.conf text
// ([Installation "name"] groups).
std::vector<std::string> parseFlatpakInstallations(std::string_view text);

// Short, safe-to-show reason from a failing command's stderr.
std::string commandFailure(std::string_view err, int exitCode);

} // namespace aqtruncat::metrics
