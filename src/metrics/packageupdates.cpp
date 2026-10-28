#include "packageupdates.h"

#include "textparse.h"

#include <algorithm>
#include <set>

namespace aqtruncat::metrics {

namespace {

bool contains(const std::vector<std::string> &list, const std::string &value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

PackageManager forDistribution(const std::string &id)
{
    if (id == "arch" || id == "archarm" || id == "manjaro" || id == "endeavouros" || id == "cachyos")
        return PackageManager::Pacman;
    if (id == "debian" || id == "ubuntu" || id == "linuxmint" || id == "pop" || id == "raspbian")
        return PackageManager::Apt;
    if (id == "fedora" || id == "rhel" || id == "centos" || id == "rocky" || id == "almalinux")
        return PackageManager::Dnf;
    return PackageManager::None;
}

bool present(const PackageManagerAvailability &available, PackageManager manager)
{
    switch (manager) {
    case PackageManager::Pacman: return available.pacman;
    case PackageManager::Apt: return available.apt;
    case PackageManager::Dnf: return available.dnf;
    case PackageManager::None: return false;
    }
    return false;
}

// checkupdates emits one "name old -> new" record for each official update.
UpdateCount parseCheckupdatesRecords(std::string_view out)
{
    UpdateCount result;
    int count = 0;
    for (const std::string_view line : splitLines(out)) {
        if (trimView(line).empty())
            continue;
        const auto fields = splitFields(line);
        if ((fields.size() != 4 && !(fields.size() == 5 && fields[4] == "[ignored]"))
            || fields[0].empty() || fields[1].empty() || fields[2] != "->" || fields[3].empty()) {
            result.error = "unexpected checkupdates output format";
            return result;
        }
        if (fields.size() == 5)
            ++result.ignored;
        else
            ++count;
    }
    result.count = count;
    return result;
}

UpdateCount failed(std::string_view err, int exitCode)
{
    UpdateCount result;
    result.error = commandFailure(err, exitCode);
    return result;
}

} // namespace

const char *packageManagerName(PackageManager manager)
{
    switch (manager) {
    case PackageManager::Pacman: return "pacman";
    case PackageManager::Apt: return "apt";
    case PackageManager::Dnf: return "dnf";
    case PackageManager::None: return "";
    }
    return "";
}

PackageManager choosePackageManager(const PackageManagerAvailability &available, const std::string &id,
                                    const std::vector<std::string> &idLike)
{
    if (const PackageManager own = forDistribution(id); present(available, own))
        return own;
    for (const std::string &like : idLike) {
        if (const PackageManager related = forDistribution(like); present(available, related))
            return related;
    }
    for (const PackageManager manager : {PackageManager::Pacman, PackageManager::Apt, PackageManager::Dnf}) {
        if (present(available, manager))
            return manager;
    }
    return PackageManager::None;
}

std::optional<CommandSpec> packageUpdateCommand(PackageManager manager, bool remote, bool checkupdatesAvailable,
                                                std::string *reason)
{
    switch (manager) {
    case PackageManager::Pacman:
        if (!checkupdatesAvailable) {
            if (reason)
                *reason = "checkupdates is not installed (pacman-contrib). Install with: sudo pacman -S pacman-contrib";
            return std::nullopt;
        }
        // Default checks reuse checkupdates' private database without network
        // access. Explicit refresh syncs that private database, never pacman's.
        return remote ? CommandSpec{"checkupdates", {"--nocolor"}}
                      : CommandSpec{"checkupdates", {"--nosync", "--nocolor"}};
    case PackageManager::Apt:
        if (remote && reason)
            *reason = "apt cannot refresh its package lists without root: using the local package lists";
        // A simulation (-s) as a normal user: nothing is locked or changed.
        return CommandSpec{"apt-get", {"-s", "-q", "-o", "Debug::NoLocking=true", "dist-upgrade"}};
    case PackageManager::Dnf:
        if (remote)
            return CommandSpec{"dnf", {"-q", "--refresh", "check-update"}};
        return CommandSpec{"dnf", {"-q", "-C", "check-update"}};
    case PackageManager::None:
        break;
    }
    if (reason)
        *reason = "no supported package manager (pacman, apt, dnf) found";
    return std::nullopt;
}

UpdateCount parseCheckupdates(std::string_view out, int exitCode, std::string_view err)
{
    if (exitCode == 2) {
        if (!trimView(out).empty()) {
            UpdateCount invalid;
            invalid.error = "unexpected checkupdates output for the no-updates status";
            return invalid;
        }
        UpdateCount none;
        none.count = 0;
        return none;
    }
    if (exitCode == 0) {
        if (trimView(out).empty()) {
            UpdateCount invalid;
            invalid.error = "checkupdates returned success without update records";
            return invalid;
        }
        return parseCheckupdatesRecords(out);
    }
    return failed(err, exitCode);
}

UpdateCount parseAptSimulation(std::string_view out, int exitCode, std::string_view err)
{
    if (exitCode != 0)
        return failed(err, exitCode);
    UpdateCount result;
    std::set<std::string_view> packages;
    for (const std::string_view line : splitLines(out)) {
        const auto fields = splitFields(line);
        if (fields.size() >= 3 && fields[0] == "Inst" && fields[2].starts_with("["))
            packages.insert(fields[1]);
    }
    result.count = int(packages.size());
    return result;
}

UpdateCount parseDnfCheckUpdate(std::string_view out, int exitCode, std::string_view err)
{
    if (exitCode == 0) {
        UpdateCount none;
        none.count = 0;
        return none;
    }
    if (exitCode != 100)
        return failed(err, exitCode);
    UpdateCount result;
    std::set<std::string_view> packages;
    for (const std::string_view line : splitLines(out)) {
        const std::string_view trimmed = trimView(line);
        // dnf4 "Obsoleting Packages" repeats packages already listed;
        // dnf5 has the same section after "Upgrades".
        if (trimmed.starts_with("Obsoleting"))
            break;
        if (line.starts_with(" ") || line.starts_with("\t"))
            continue; // continuation lines of the obsoletes section
        const auto fields = splitFields(line);
        if (fields.size() != 3)
            continue;
        const std::string_view name = fields[0];
        const std::size_t dot = name.rfind('.');
        if (dot == std::string_view::npos || dot == 0 || dot + 1 == name.size())
            continue;
        if (fields[1].empty() || !(fields[1].front() >= '0' && fields[1].front() <= '9'))
            continue; // epochs ("2:1.0") and versions start with a digit
        packages.insert(name);
    }
    result.count = int(packages.size());
    return result;
}

UpdateCount parseFlatpakRefs(std::string_view out, int exitCode, std::string_view err)
{
    if (exitCode != 0)
        return failed(err, exitCode);
    UpdateCount result;
    std::set<std::string_view> refs;
    for (const std::string_view line : splitLines(out)) {
        const std::string_view ref = trimView(line);
        if (std::count(ref.begin(), ref.end(), '/') >= 2 && ref.find(' ') == std::string_view::npos)
            refs.insert(ref);
    }
    result.count = int(refs.size());
    return result;
}

std::vector<std::string> parseFlatpakInstallations(std::string_view text)
{
    std::vector<std::string> names;
    for (const std::string_view rawLine : splitLines(text)) {
        const std::string_view line = trimView(rawLine);
        constexpr std::string_view prefix = "[Installation \"";
        if (!line.starts_with(prefix) || !line.ends_with("\"]"))
            continue;
        const std::string name(line.substr(prefix.size(), line.size() - prefix.size() - 2));
        const bool valid = !name.empty() && name.size() <= 64
            && std::all_of(name.begin(), name.end(), [](char c) {
                   return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'
                       || c == '_' || c == '.';
               });
        if (valid && !contains(names, name))
            names.push_back(name);
    }
    return names;
}

std::string commandFailure(std::string_view err, int exitCode)
{
    // The first meaningful stderr line, bounded: it is shown to the user.
    for (const std::string_view line : splitLines(err)) {
        std::string_view text = trimView(line);
        if (text.empty())
            continue;
        if (text.size() > 200)
            text = text.substr(0, 200);
        std::string clean;
        for (const char c : text)
            clean += (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) ? ' ' : c;
        return clean;
    }
    return "the check failed (exit status " + std::to_string(exitCode) + ")";
}

} // namespace aqtruncat::metrics
