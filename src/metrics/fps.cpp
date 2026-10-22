#include "fps.h"

#include "textparse.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sys/stat.h>

namespace aqtruncat::metrics {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t MaxConfigBytes = 256 * 1024;
constexpr std::size_t HeaderBytes = 8 * 1024;
constexpr std::size_t TailBytes = 16 * 1024;
constexpr double RescanSeconds = 5.0;

std::optional<double> number(std::string_view text)
{
    text = trimView(text);
    double value = 0.0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || ptr != text.data() + text.size() || text.empty() || !std::isfinite(value))
        return std::nullopt;
    return value;
}

std::vector<std::string_view> csvFields(std::string_view line)
{
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = line.find(',', start);
        fields.push_back(trimView(line.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start)));
        if (comma == std::string_view::npos)
            break;
        start = comma + 1;
    }
    return fields;
}

double mtimeOf(const struct stat &st)
{
    return double(st.st_mtim.tv_sec) + double(st.st_mtim.tv_nsec) / 1e9;
}

// Reads [offset, offset+length) of a file.
std::optional<std::string> readRange(const std::string &path, std::uint64_t offset, std::size_t length)
{
    std::FILE *file = std::fopen(path.c_str(), "re");
    if (!file)
        return std::nullopt;
    std::string data(length, '\0');
    std::size_t n = 0;
    if (std::fseek(file, long(offset), SEEK_SET) == 0)
        n = std::fread(data.data(), 1, length, file);
    std::fclose(file);
    data.resize(n);
    return data;
}

bool isLogName(const std::string &name)
{
    return name.size() > 4 && name.ends_with(".csv") && !name.ends_with("_summary.csv");
}

} // namespace

std::vector<std::string> mangoHudOutputFolders(std::string_view configText, const std::string &home)
{
    std::vector<std::string> folders;
    for (std::string_view line : splitLines(configText)) {
        line = trimView(line);
        if (line.empty() || line.front() == '#')
            continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || trimView(line.substr(0, equals)) != "output_folder")
            continue;
        std::string value(trimView(line.substr(equals + 1)));
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
            value = value.substr(1, value.size() - 2);
        if (value == "~" || value.starts_with("~/"))
            value = home + value.substr(1);
        if (!value.empty() && value.front() == '/' && std::find(folders.begin(), folders.end(), value) == folders.end())
            folders.push_back(value);
    }
    return folders;
}

std::optional<MangoHudRow> parseMangoHudTail(std::string_view header, std::string_view tail, std::string *error)
{
    const auto columns = csvFields(trimView(header));
    const auto fpsColumn = std::find(columns.begin(), columns.end(), "fps");
    if (fpsColumn == columns.end()) {
        if (error)
            *error = "the MangoHud log has no fps column";
        return std::nullopt;
    }
    const std::size_t fpsIndex = std::size_t(fpsColumn - columns.begin());
    const auto frametimeColumn = std::find(columns.begin(), columns.end(), "frametime");
    // Only rows that end with a newline are complete: MangoHud may be in
    // the middle of writing the last one.
    const std::size_t lastNewline = tail.rfind('\n');
    if (lastNewline == std::string_view::npos) {
        if (error)
            *error = "no complete row yet";
        return std::nullopt;
    }
    const auto lines = splitLines(tail.substr(0, lastNewline));
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        const std::string_view line = trimView(*it);
        if (line.empty())
            continue;
        const auto fields = csvFields(line);
        if (fields == columns)
            break; // the header: no data row yet
        if (fields.size() != columns.size()) {
            if (error)
                *error = "malformed row in the MangoHud log";
            return std::nullopt;
        }
        const auto fps = number(fields[fpsIndex]);
        if (!fps || *fps < 0.0 || *fps > 100000.0) {
            if (error)
                *error = "malformed fps value in the MangoHud log";
            return std::nullopt;
        }
        MangoHudRow row;
        row.fps = *fps;
        if (frametimeColumn != columns.end()) {
            const auto frametime = number(fields[std::size_t(frametimeColumn - columns.begin())]);
            if (frametime && *frametime >= 0.0)
                row.frametimeMs = *frametime;
        }
        return row;
    }
    if (error)
        *error = "no data row yet";
    return std::nullopt;
}

std::string mangoHudApplication(const std::string &fileName)
{
    std::string name = fileName.ends_with(".csv") ? fileName.substr(0, fileName.size() - 4) : fileName;
    // "_YYYY-MM-DD_HH-MM-SS"
    if (name.size() > 20) {
        const std::string suffix = name.substr(name.size() - 20);
        const bool stamp = suffix[0] == '_' && suffix[5] == '-' && suffix[8] == '-' && suffix[11] == '_'
            && suffix[14] == '-' && suffix[17] == '-';
        if (stamp)
            name.resize(name.size() - 20);
    }
    return name;
}

MangoHudFpsReader::MangoHudFpsReader(FpsSources sources)
    : m_sources(std::move(sources))
{
}

bool MangoHudFpsReader::installed() const
{
    std::error_code ec;
    for (const std::string &dir : m_sources.layerDirs) {
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::string name = it->path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (name.starts_with("mangohud") && name.ends_with(".json"))
                return true;
        }
    }
    for (const std::string &dir : m_sources.binaryDirs) {
        if (fs::exists(dir + "/mangohud", ec))
            return true;
    }
    return false;
}

std::vector<std::string> MangoHudFpsReader::outputFolders() const
{
    // MangoHud.conf and the per-application files next to it.
    std::vector<std::string> folders;
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::directory_iterator it(m_sources.configDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".conf")
            files.push_back(it->path().string());
    }
    std::sort(files.begin(), files.end());
    for (const std::string &file : files) {
        const auto text = readBoundedTextFile(file, MaxConfigBytes);
        if (!text)
            continue;
        for (const std::string &folder : mangoHudOutputFolders(*text, m_sources.home)) {
            if (std::find(folders.begin(), folders.end(), folder) == folders.end())
                folders.push_back(folder);
        }
    }
    return folders;
}

std::optional<MangoHudFpsReader::LogFile> MangoHudFpsReader::newestLog(const std::vector<std::string> &folders, double now)
{
    bool rescan = now - m_lastScan >= RescanSeconds || !m_newest;
    std::map<std::string, double> folderTimes;
    for (const std::string &folder : folders) {
        struct stat st {};
        if (::stat(folder.c_str(), &st) == 0) {
            folderTimes[folder] = mtimeOf(st);
            const auto previous = m_folderTimes.find(folder);
            if (previous == m_folderTimes.end() || previous->second != folderTimes[folder])
                rescan = true; // a log was created or removed
        }
    }
    if (folderTimes != m_folderTimes)
        rescan = true;
    m_folderTimes = std::move(folderTimes);

    if (rescan) {
        m_lastScan = now;
        m_newest.reset();
        std::error_code ec;
        for (const std::string &folder : folders) {
            for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
                const std::string name = it->path().filename().string();
                if (!isLogName(name))
                    continue;
                struct stat st {};
                const std::string path = it->path().string();
                if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
                    continue;
                if (!m_newest || mtimeOf(st) > m_newest->mtime)
                    m_newest = LogFile{path, mtimeOf(st), std::uint64_t(st.st_size)};
            }
        }
        return m_newest;
    }
    if (m_newest) {
        struct stat st {};
        if (::stat(m_newest->path.c_str(), &st) != 0) {
            m_newest.reset();
            m_lastScan = -1e9;
        } else {
            m_newest->mtime = mtimeOf(st);
            m_newest->size = std::uint64_t(st.st_size);
        }
    }
    return m_newest;
}

FpsMetrics MangoHudFpsReader::sample(const std::string &source)
{
    FpsMetrics result;
    if (source != FpsSourceMangoHud) {
        result.condition = FpsCondition::Disabled;
        result.status = MetricStatus::unavailable("the FPS source is turned off");
        return result;
    }
    result.source = FpsSourceMangoHud;
    const double now = m_sources.wallClock ? m_sources.wallClock() : 0.0;
    if (now - m_lastConfigRead >= RescanSeconds || now < m_lastConfigRead) {
        m_lastConfigRead = now;
        m_folders = outputFolders();
        m_installed = m_folders.empty() && installed();
    }
    const std::vector<std::string> &folders = m_folders;
    if (folders.empty()) {
        if (m_installed) {
            result.condition = FpsCondition::NotConfigured;
            result.status = MetricStatus::unavailable(
                "MangoHud has no output_folder: set output_folder and log_interval in MangoHud.conf, then start logging");
        } else {
            result.condition = FpsCondition::Missing;
            result.status = MetricStatus::unavailable("MangoHud is not installed");
        }
        return result;
    }
    const auto log = newestLog(folders, now);
    if (!log) {
        result.condition = FpsCondition::Inactive;
        result.status = MetricStatus::unavailable("no MangoHud log yet: start logging in a game (Shift_L+F2 or autostart_log)");
        return result;
    }
    result.application = mangoHudApplication(fs::path(log->path).filename().string());
    result.ageSeconds = std::max(0.0, now - log->mtime);
    if (*result.ageSeconds > InactiveSeconds) {
        result.condition = FpsCondition::Inactive;
        result.status = MetricStatus::unavailable("no game is logging with MangoHud now");
        return result;
    }

    if (m_headerPath != log->path) {
        // The column line: the first one that starts with "fps," (the
        // versioned format has a few header lines before it).
        m_header.clear();
        m_headerPath = log->path;
        if (const auto head = readRange(log->path, 0, HeaderBytes)) {
            for (const std::string_view line : splitLines(*head)) {
                if (trimView(line).starts_with("fps,")) {
                    m_header = std::string(trimView(line));
                    break;
                }
            }
        }
    }
    if (m_header.empty()) {
        m_headerPath.clear(); // try again next time: the header may not be written yet
        result.condition = FpsCondition::Unsupported;
        result.status = MetricStatus::error("the MangoHud log has no frame-metrics header");
        return result;
    }
    const std::uint64_t offset = log->size > TailBytes ? log->size - TailBytes : 0;
    const auto tail = readRange(log->path, offset, std::size_t(std::min<std::uint64_t>(log->size, TailBytes)));
    std::string error;
    const auto row = tail ? parseMangoHudTail(m_header, *tail, &error) : std::nullopt;
    if (!row) {
        result.condition = FpsCondition::Unsupported;
        result.status = error == "no complete row yet" || error == "no data row yet"
            ? MetricStatus::pending("waiting for the first MangoHud row")
            : MetricStatus::error(tail ? error : log->path + ": unreadable");
        if (result.status.state == MetricState::Pending)
            result.condition = FpsCondition::Inactive;
        return result;
    }
    result.lastFps = row->fps;
    if (*result.ageSeconds > FreshSeconds) {
        result.condition = FpsCondition::Stale;
        result.status = MetricStatus::unavailable("the MangoHud log has not been written for "
                                                  + std::to_string(int(*result.ageSeconds)) + " s");
        return result;
    }
    result.condition = FpsCondition::Measured;
    result.fps = row->fps;
    result.frametimeMs = row->frametimeMs;
    result.status = MetricStatus::okStatus();
    return result;
}

} // namespace aqtruncat::metrics
