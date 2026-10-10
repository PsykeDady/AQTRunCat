#pragma once

namespace aqtruncat {

// Why a runner operation failed. Frontends (CLI and Settings UI) map it to
// their own presentation, e.g. the CLI exit status.
enum class RunnerError {
    None,
    NotFound,   // no runner with that id
    Invalid,    // runner, archive or id rejected by validation
    Conflict,   // already installed, bundled runner protected, ambiguous id
    Filesystem, // reading or writing files failed
};

} // namespace aqtruncat
