#include "systeminfo.h"

#include "textparse.h"

#include <cerrno>
#include <cstring>
#include <sys/utsname.h>

namespace aqtruncat::metrics {

namespace {

// Release files are a few hundred bytes; anything larger is not one.
constexpr std::size_t MaxReleaseFileBytes = 64 * 1024;

bool validKey(std::string_view key)
{
    if (key.empty())
        return false;
    for (const char c : key) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}

// The value part of one assignment; nullopt when it is malformed.
std::optional<std::string> parseValue(std::string_view raw)
{
    std::string value;
    if (raw.empty())
        return value;
    const char quote = raw.front();
    if (quote == '"' || quote == '\'') {
        bool closed = false;
        std::size_t i = 1;
        for (; i < raw.size(); ++i) {
            const char c = raw[i];
            if (c == quote) {
                closed = true;
                break;
            }
            if (quote == '"' && c == '\\' && i + 1 < raw.size()) {
                const char next = raw[i + 1];
                if (next == '"' || next == '\\' || next == '$' || next == '`') {
                    value += next;
                    ++i;
                    continue;
                }
            }
            value += c;
        }
        // Nothing but spaces may follow the closing quote.
        if (!closed || !trimView(raw.substr(i + 1)).empty())
            return std::nullopt;
        return value;
    }
    for (const char c : raw) {
        // Unquoted values are single words in the specification.
        if (c == ' ' || c == '\t' || c == '"' || c == '\'' || c == '\\' || c == '$' || c == '`')
            return std::nullopt;
    }
    return std::string(raw);
}

std::string valueOf(const std::map<std::string, std::string> &values, const char *key)
{
    const auto it = values.find(key);
    return it == values.end() ? std::string() : it->second;
}

std::vector<std::string> words(const std::string &text)
{
    std::vector<std::string> result;
    for (const std::string_view field : splitFields(text))
        result.emplace_back(field);
    return result;
}

std::optional<std::string> readRelease(const std::string &path)
{
    return readBoundedTextFile(path, MaxReleaseFileBytes);
}

// First nonempty line of a free-text release file ("Fedora release 44 (…)").
std::string firstLine(const std::string &text)
{
    for (const std::string_view line : splitLines(text)) {
        const std::string_view trimmed = trimView(line);
        if (!trimmed.empty())
            return std::string(trimmed.substr(0, 256));
    }
    return {};
}

} // namespace

std::map<std::string, std::string> parseOsRelease(std::string_view text, int *malformedLines)
{
    std::map<std::string, std::string> values;
    int malformed = 0;
    for (const std::string_view rawLine : splitLines(text)) {
        const std::string_view line = trimView(rawLine);
        if (line.empty() || line.front() == '#')
            continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || !validKey(line.substr(0, equals))) {
            ++malformed;
            continue;
        }
        const auto value = parseValue(line.substr(equals + 1));
        if (!value) {
            ++malformed;
            continue;
        }
        values[std::string(line.substr(0, equals))] = *value;
    }
    if (malformedLines)
        *malformedLines = malformed;
    return values;
}

std::optional<KernelInfo> queryKernel(std::string *error)
{
    struct utsname name {};
    if (::uname(&name) != 0) {
        if (error)
            *error = std::string("uname: ") + std::strerror(errno);
        return std::nullopt;
    }
    return KernelInfo{name.sysname, name.release, name.version, name.machine};
}

DistributionMetrics readDistribution(const std::string &root)
{
    DistributionMetrics result;
    for (const char *relative : {"/etc/os-release", "/usr/lib/os-release"}) {
        const std::string path = root + relative;
        const auto text = readRelease(path);
        if (!text)
            continue;
        const auto values = parseOsRelease(*text);
        DistributionInfo info;
        info.name = valueOf(values, "NAME");
        info.id = valueOf(values, "ID");
        info.idLike = words(valueOf(values, "ID_LIKE"));
        info.versionId = valueOf(values, "VERSION_ID");
        info.version = valueOf(values, "VERSION");
        info.codename = valueOf(values, "VERSION_CODENAME");
        info.buildId = valueOf(values, "BUILD_ID");
        info.variant = valueOf(values, "VARIANT");
        info.prettyName = valueOf(values, "PRETTY_NAME");
        if (info.prettyName.empty() && !info.name.empty())
            info.prettyName = info.version.empty() ? info.name : info.name + " " + info.version;
        if (info.prettyName.empty() && info.id.empty())
            continue; // nothing usable in this file: try the next source
        info.source = relative;
        result.info = std::move(info);
        result.status = MetricStatus::okStatus();
        return result;
    }

    if (const auto text = readRelease(root + "/etc/lsb-release")) {
        const auto values = parseOsRelease(*text);
        DistributionInfo info;
        info.name = valueOf(values, "DISTRIB_ID");
        info.versionId = valueOf(values, "DISTRIB_RELEASE");
        info.codename = valueOf(values, "DISTRIB_CODENAME");
        info.prettyName = valueOf(values, "DISTRIB_DESCRIPTION");
        if (info.prettyName.empty())
            info.prettyName = info.versionId.empty() ? info.name : info.name + " " + info.versionId;
        if (!info.prettyName.empty()) {
            for (char c : info.name)
                info.id += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            info.source = "/etc/lsb-release";
            result.info = std::move(info);
            result.status = MetricStatus::okStatus();
            return result;
        }
    }

    // Distribution-specific markers, for systems without os-release.
    struct Marker { const char *file; const char *id; const char *name; bool textIsName; bool textIsVersion; };
    static const Marker markers[] = {
        {"/etc/arch-release", "arch", "Arch Linux", false, false},
        {"/etc/fedora-release", "fedora", "Fedora", true, false},
        {"/etc/redhat-release", "rhel", "Red Hat family", true, false},
        {"/etc/debian_version", "debian", "Debian", false, true},
    };
    for (const Marker &marker : markers) {
        const auto text = readRelease(root + marker.file);
        if (!text)
            continue;
        DistributionInfo info;
        info.id = marker.id;
        info.name = marker.name;
        const std::string line = firstLine(*text);
        if (marker.textIsVersion)
            info.versionId = line;
        info.prettyName = marker.textIsName && !line.empty() ? line
            : info.versionId.empty()                       ? info.name
                                                           : info.name + " " + info.versionId;
        info.source = marker.file;
        result.info = std::move(info);
        result.status = MetricStatus::okStatus();
        return result;
    }
    result.status = MetricStatus::unavailable("no os-release or distribution release file");
    return result;
}

} // namespace aqtruncat::metrics
