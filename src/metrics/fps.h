#pragma once

// F5.3 gaming FPS from an optional, external source. There is no general
// way to read another application's frame rate on Linux without injecting
// code into it; AQTRunCat therefore only reads what MangoHud, already
// loaded by the user into a game, writes on its own: its CSV frame log
// (output_folder, appended and flushed row by row while logging). Nothing
// is injected, no overlay is required, no process memory is read. FPS is
// never derived from GPU/CPU load or from the display refresh rate.

#include "metricstatus.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aqtruncat::metrics {

enum class FpsCondition {
    Measured,      // a fresh reading from the active source
    Stale,         // the source stopped writing a short while ago
    Inactive,      // the source is set up, but nothing is logging now
    NotConfigured, // MangoHud is installed but has no output_folder
    Missing,       // MangoHud is not installed
    Disabled,      // the FPS source is turned off in the settings
    Unsupported,   // a log in a format this version cannot read
};

struct FpsMetrics {
    MetricStatus status;
    // Not Disabled by default: only an explicit OFF source hides FPS.
    FpsCondition condition = FpsCondition::Inactive;
    std::string source;      // "mangohud", or empty when off
    std::optional<double> fps;            // only when Measured
    std::optional<double> frametimeMs;    // with fps, when logged
    std::optional<double> lastFps;        // the last reading, also when Stale
    std::optional<double> ageSeconds;     // since the log was last written
    std::string application; // from the log name ("Game" of Game_2026-…csv)
};

// FPS source OFF is the user's choice not to see FPS, not a missing
// reading: every presentation (Dashboard, Metrics Bar, Plasmoid) hides the
// metric then, while the API keeps reporting it as "disabled". Every other
// condition stays visible with its status.
inline bool fpsHidden(const FpsMetrics &fps) { return fps.condition == FpsCondition::Disabled; }

inline constexpr const char *FpsSourceMangoHud = "mangohud";
inline constexpr const char *FpsSourceOff = "off";
// A reading older than this is stale, older than InactiveSeconds inactive.
inline constexpr double FreshSeconds = 3.0;
inline constexpr double InactiveSeconds = 60.0;

// output_folder values of MangoHud configuration text ("key=value" lines,
// '#' comments); "~" expands to home.
std::vector<std::string> mangoHudOutputFolders(std::string_view configText, const std::string &home);

// The fps (and frametime) of the last complete row of a MangoHud log.
// header: the column line ("fps,frametime,…"); tail: the end of the file.
struct MangoHudRow {
    double fps = 0.0;
    std::optional<double> frametimeMs;
};
std::optional<MangoHudRow> parseMangoHudTail(std::string_view header, std::string_view tail, std::string *error);

// "Game_2026-10-22_10-31-02.csv" → "Game".
std::string mangoHudApplication(const std::string &fileName);

struct FpsSources {
    std::string configDir;  // ~/.config/MangoHud
    std::string home;
    std::vector<std::string> layerDirs;  // Vulkan implicit layer manifests
    std::vector<std::string> binaryDirs; // PATH entries (for "mangohud")
    std::function<double()> wallClock;   // seconds since the epoch
};

class MangoHudFpsReader
{
public:
    explicit MangoHudFpsReader(FpsSources sources);

    FpsMetrics sample(const std::string &source);

private:
    struct LogFile {
        std::string path;
        double mtime = 0.0;
        std::uint64_t size = 0;
    };
    bool installed() const;
    std::vector<std::string> outputFolders() const;
    std::optional<LogFile> newestLog(const std::vector<std::string> &folders, double now);

    FpsSources m_sources;
    // Directory listings are refreshed when a folder changes, at most every
    // RescanSeconds otherwise; the newest file itself is checked each time.
    // MangoHud's configuration (and whether it is installed) is re-read at
    // most every RescanSeconds, not with every sample.
    std::vector<std::string> m_folders;
    bool m_installed = false;
    double m_lastConfigRead = -1e9;
    std::optional<LogFile> m_newest;
    std::map<std::string, double> m_folderTimes;
    double m_lastScan = -1e9;
    std::string m_headerPath;
    std::string m_header;
};

} // namespace aqtruncat::metrics
