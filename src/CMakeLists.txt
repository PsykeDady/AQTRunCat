# Qt-free logic: CPU parsing/delta, smoothing, speed curve, frame playback. Unit tested.
add_library(aqtruncat_core STATIC
    core/cpustat.cpp
    core/cpustat.h
    core/frameplayback.h
    core/smoothing.h
    core/speedcurve.cpp
    core/speedcurve.h)
target_include_directories(aqtruncat_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(aqtruncat_core PRIVATE -Wall -Wextra -Wpedantic)

# Runner model, loading, discovery, lifecycle (import, install, remove) and
# frame timing. Knows nothing about CPU, tray or command line. Shared by every
# frontend (CLI and Settings UI).
add_library(aqtruncat_runner STATIC
    runner/framenames.cpp
    runner/framenames.h
    runner/runner.h
    runner/runneranimator.cpp
    runner/runneranimator.h
    runner/runnerarchive.cpp
    runner/runnerarchive.h
    runner/runnercatalog.cpp
    runner/runnercatalog.h
    runner/runnererror.h
    runner/runnerinstaller.cpp
    runner/runnerinstaller.h
    runner/runnerloader.cpp
    runner/runnerloader.h
    runner/runnerremover.cpp
    runner/runnerremover.h
    runner/runnerservice.cpp
    runner/runnerservice.h)
target_link_libraries(aqtruncat_runner PUBLIC aqtruncat_core Qt6::Gui PRIVATE LibArchive::LibArchive)
target_compile_options(aqtruncat_runner PRIVATE -Wall -Wextra -Wpedantic)

# Linux system metrics (CPU, memory, network, storage, disk I/O; F5: uptime,
# GPU, FPS from MangoHud logs, kernel/distribution, update-check parsing)
# from /proc, /sys, statvfs and release files. Plain C++, no Qt; reuses core/cpustat for CPU usage.
# Shared by every consumer (CLI, Dashboard and Metrics Bar).
add_library(aqtruncat_metrics STATIC
    metrics/compactformat.cpp
    metrics/compactformat.h
    metrics/counterrates.h
    metrics/diskio.cpp
    metrics/diskio.h
    metrics/fps.cpp
    metrics/fps.h
    metrics/gpu.cpp
    metrics/gpu.h
    metrics/memory.cpp
    metrics/memory.h
    metrics/metricsformat.cpp
    metrics/metricsformat.h
    metrics/metricstatus.h
    metrics/network.cpp
    metrics/network.h
    metrics/packageupdates.cpp
    metrics/packageupdates.h
    metrics/storage.cpp
    metrics/storage.h
    metrics/systeminfo.cpp
    metrics/systeminfo.h
    metrics/systemmetrics.cpp
    metrics/systemmetrics.h
    metrics/textparse.cpp
    metrics/textparse.h
    metrics/uptime.cpp
    metrics/uptime.h)
# dlopen of NVML (F5.2): optional at run time, no link dependency.
target_link_libraries(aqtruncat_metrics PUBLIC aqtruncat_core PRIVATE ${CMAKE_DL_LIBS})
target_compile_options(aqtruncat_metrics PRIVATE -Wall -Wextra -Wpedantic)

# Command line: grammar, help, "runner …" and "metrics" commands. Unit tested.
add_library(aqtruncat_cli STATIC
    cli/commandline.cpp
    cli/commandline.h
    cli/metricscommand.cpp
    cli/metricscommand.h
    cli/runnercommands.cpp
    cli/runnercommands.h)
target_link_libraries(aqtruncat_cli PUBLIC aqtruncat_runner aqtruncat_metrics)
target_compile_options(aqtruncat_cli PRIVATE -Wall -Wextra -Wpedantic)

# Engine: settings, the system-metrics sampler, Custom Metrics (F1, with the
# F2/F3 adapters it runs), runner speed, autostart and official-integration
# management. QtCore only: the headless backend (aqtruncatd) is built on it,
# and it never needs a display, a tray or Qt Widgets/Gui.
add_library(aqtruncat_engine STATIC
    custommetrics/commandmetric.cpp
    custommetrics/commandmetric.h
    custommetrics/custommetric.cpp
    custommetrics/custommetric.h
    custommetrics/metricgroup.cpp
    custommetrics/metricgroup.h
    app/applicationquit.cpp
    app/applicationquit.h
    app/appsettings.cpp
    app/appsettings.h
    app/autostartcommand.cpp
    app/autostartcommand.h
    app/autostartmanager.cpp
    app/autostartmanager.h
    app/boundedprocess.cpp
    app/boundedprocess.h
    app/cpumonitor.cpp
    app/cpumonitor.h
    app/custommetricpaths.cpp
    app/custommetricpaths.h
    app/custommetricsmodel.h
    app/custommetricsservice.cpp
    app/custommetricsservice.h
    app/hoversummary.cpp
    app/hoversummary.h
    app/metricsampler.cpp
    app/metricsampler.h
    app/metricsbarids.cpp
    app/metricsbarids.h
    app/processgroup.h
    app/providerintegration.cpp
    app/providerintegration.h
    app/runpreferences.cpp
    app/runpreferences.h
    app/runnerspeedcontroller.cpp
    app/runnerspeedcontroller.h
    app/snapshotthrottle.cpp
    app/snapshotthrottle.h
    app/storagepaths.cpp
    app/storagepaths.h
    app/updatesservice.cpp
    app/updatesservice.h)
target_link_libraries(aqtruncat_engine PUBLIC aqtruncat_metrics Qt6::Core)
target_compile_options(aqtruncat_engine PRIVATE -Wall -Wextra -Wpedantic)

# F4.1 public API: schema, the backend service and the client used by the
# frontends. QtCore + QtDBus only (no Widgets, no Gui).
add_library(aqtruncat_api STATIC
    api/apischema.cpp
    api/apischema.h
    api/backendclient.cpp
    api/backendclient.h
    api/backendservice.cpp
    api/backendservice.h)
target_link_libraries(aqtruncat_api PUBLIC aqtruncat_engine Qt6::Core Qt6::DBus)
target_compile_options(aqtruncat_api PRIVATE -Wall -Wextra -Wpedantic)

# F4.2: runner selection (shared with the Qt frontend) and the GetRunners
# catalogue. Needs QtGui through the runner library, so it stays out of
# aqtruncat_api: only aqtruncatd and the Qt frontend link it.
add_library(aqtruncat_runner_catalog STATIC
    api/runnercatalog.cpp
    api/runnercatalog.h
    app/runnerselection.cpp
    app/runnerselection.h)
target_link_libraries(aqtruncat_runner_catalog PUBLIC aqtruncat_api aqtruncat_runner)
target_compile_options(aqtruncat_runner_catalog PRIVATE -Wall -Wextra -Wpedantic)

# Desktop frontend (Qt Widgets / SNI tray): Dashboard, Settings, Metrics
# Bar, runner presentation. Since F4.1 it consumes the backend over D-Bus.
add_library(aqtruncat_desktop STATIC
    app/customsymbol.cpp
    app/customsymbol.h
    app/dashboardwindow.cpp
    app/flowlayout.cpp
    app/flowlayout.h
    app/dashboardwindow.h
    app/metricsbarmodel.cpp
    app/metricsbarmodel.h
    app/metricsbarpresenter.cpp
    app/metricsbarpresenter.h
    app/metricsbarrenderer.cpp
    app/metricsbarrenderer.h
    app/runnerpresentation.cpp
    app/runnerpresentation.h
    app/runnerpreviewwidget.cpp
    app/runnerpreviewwidget.h
    app/settingswindow.cpp
    app/settingswindow.h
    app/traypresenter.cpp
    app/traypresenter.h)
target_link_libraries(aqtruncat_desktop PUBLIC aqtruncat_api aqtruncat_engine aqtruncat_runner_catalog Qt6::Widgets)
target_compile_options(aqtruncat_desktop PRIVATE -Wall -Wextra -Wpedantic)

qt_add_executable(aqtruncat
    app/logging.cpp
    app/logging.h
    app/main.cpp
    app/trayapp.cpp
    app/trayapp.h)
target_link_libraries(aqtruncat PRIVATE aqtruncat_desktop aqtruncat_cli Qt6::Widgets)
target_compile_options(aqtruncat PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS aqtruncat RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Bundled runners, compiled in as :/runners/<id>/…: frames from third_party
# (see THIRD_PARTY_NOTICES.md) and AQTRunCat's own metadata.json from
# data/runners/<id>/. F7 added Walking Goose, Party Parrot and Engine, v2F1
# (2.1.1) Rickroll.
set(aqtruncat_default_runners "cat;coffee;walkinggoose;partyparrot;engine;rickroll")
# A build tree configured before a runner was added keeps a cached list:
# follow the new default unless the cached list was customised (the cached
# default it was set from; "cat;coffee" before F7 recorded it).
if(DEFINED CACHE{AQTRUNCAT_BUNDLED_RUNNERS_DEFAULT})
    set(previous_default_runners "${AQTRUNCAT_BUNDLED_RUNNERS_DEFAULT}")
else()
    set(previous_default_runners "cat;coffee")
endif()
if(NOT DEFINED CACHE{AQTRUNCAT_BUNDLED_RUNNERS}
   OR AQTRUNCAT_BUNDLED_RUNNERS STREQUAL previous_default_runners)
    set(AQTRUNCAT_BUNDLED_RUNNERS "${aqtruncat_default_runners}" CACHE STRING "Runner ids shipped inside the executable" FORCE)
endif()
set(AQTRUNCAT_BUNDLED_RUNNERS_DEFAULT "${aqtruncat_default_runners}" CACHE INTERNAL "default of AQTRUNCAT_BUNDLED_RUNNERS")
set(AQTRUNCAT_DEFAULT_RUNNER "cat" CACHE STRING "Runner shown when none is requested")
if(NOT AQTRUNCAT_DEFAULT_RUNNER IN_LIST AQTRUNCAT_BUNDLED_RUNNERS)
    message(FATAL_ERROR "AQTRUNCAT_DEFAULT_RUNNER '${AQTRUNCAT_DEFAULT_RUNNER}' is not in AQTRUNCAT_BUNDLED_RUNNERS")
endif()
target_compile_definitions(aqtruncat PRIVATE
    AQTRUNCAT_VERSION="${PROJECT_VERSION}"
    AQTRUNCAT_DEFAULT_RUNNER="${AQTRUNCAT_DEFAULT_RUNNER}")

# Where the frames of each runner are: RunCat Neo's runners by id, the F7
# and v2 runners in their own third_party folders (the runner id is the installed
# directory name, whatever the source folder is called).
set(runner_frames_walkinggoose "${PROJECT_SOURCE_DIR}/third_party/walkinggoose/runners/goose")
set(runner_frames_partyparrot "${PROJECT_SOURCE_DIR}/third_party/partyparrot/runners/parrot")
set(runner_frames_rickroll "${PROJECT_SOURCE_DIR}/third_party/rickroll/runners/rickroll")
set(metadata_root "${PROJECT_SOURCE_DIR}/data/runners")
set(bundled_metadata "")
foreach(runner IN LISTS AQTRUNCAT_BUNDLED_RUNNERS)
    if(DEFINED runner_frames_${runner})
        set(frames_dir "${runner_frames_${runner}}")
    else()
        set(frames_dir "${PROJECT_SOURCE_DIR}/third_party/runcatneo/runners/${runner}")
    endif()
    file(GLOB frames "${frames_dir}/*.png")
    if(NOT frames)
        message(FATAL_ERROR "bundled runner '${runner}' has no frames in ${frames_dir}")
    endif()
    qt_add_resources(aqtruncat bundled_runner_${runner} PREFIX /runners/${runner} BASE "${frames_dir}" FILES ${frames})
    set(runner_files ${frames})
    if(EXISTS "${metadata_root}/${runner}/metadata.json")
        list(APPEND bundled_metadata "${metadata_root}/${runner}/metadata.json")
        list(APPEND runner_files "${metadata_root}/${runner}/metadata.json")
    endif()
    # F4.2: the same files under share/aqtruncat/runners/<id>/, for frontends
    # outside this executable (the Plasmoid loads the frames that GetRunners
    # lists); also copied into the build tree next to src/, so that a
    # build-tree aqtruncatd finds them as ../share/aqtruncat/runners.
    install(FILES ${runner_files} DESTINATION ${CMAKE_INSTALL_DATADIR}/aqtruncat/runners/${runner})
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/share/aqtruncat/runners/${runner}")
    file(COPY ${runner_files} DESTINATION "${PROJECT_BINARY_DIR}/share/aqtruncat/runners/${runner}")
endforeach()
if(bundled_metadata)
    qt_add_resources(aqtruncat bundled_runner_metadata PREFIX /runners BASE "${metadata_root}" FILES ${bundled_metadata})
endif()

# The backend (F4.1): one per user session, D-Bus activated, headless.
# (After AQTRUNCAT_DEFAULT_RUNNER is defined.)
qt_add_executable(aqtruncatd daemon/main.cpp)
target_link_libraries(aqtruncatd PRIVATE aqtruncat_api aqtruncat_runner_catalog Qt6::Core Qt6::DBus)
target_compile_options(aqtruncatd PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(aqtruncatd PRIVATE
    AQTRUNCAT_VERSION="${PROJECT_VERSION}"
    AQTRUNCAT_DEFAULT_RUNNER="${AQTRUNCAT_DEFAULT_RUNNER}")
install(TARGETS aqtruncatd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Reference CLI client of the backend API (F4.1): QtCore + QtDBus.
qt_add_executable(aqtruncatctl ctl/main.cpp)
target_link_libraries(aqtruncatctl PRIVATE aqtruncat_api Qt6::Core Qt6::DBus)
target_compile_options(aqtruncatctl PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(aqtruncatctl PRIVATE AQTRUNCAT_VERSION="${PROJECT_VERSION}")
install(TARGETS aqtruncatctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# F2 Claude Code adapter: provider acquisition kept out of the core. It feeds
# F1 command-backed Custom Metrics ("output": "snapshot") as a separate,
# QtCore-only executable; nothing in aqtruncat_desktop links it.
# Provider adapters (F2 Claude Code, F3 Codex): shared QtCore quota
# judgement/formatting, never linked by the core.
add_library(aqtruncat_provider_common STATIC
    providers/common/package.cpp
    providers/common/package.h
    providers/common/quota.cpp
    providers/common/quota.h)
target_include_directories(aqtruncat_provider_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/providers")
target_link_libraries(aqtruncat_provider_common PUBLIC Qt6::Core)
target_compile_options(aqtruncat_provider_common PRIVATE -Wall -Wextra -Wpedantic)

add_library(aqtruncat_claude STATIC
    providers/claude/claudecode.cpp
    providers/claude/claudecode.h
    providers/claude/claudeusage.cpp
    providers/claude/claudeusage.h
    providers/claude/statuslinebridge.cpp
    providers/claude/statuslinebridge.h)
target_include_directories(aqtruncat_claude PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/providers")
target_link_libraries(aqtruncat_claude PUBLIC aqtruncat_provider_common Qt6::Core)
target_compile_options(aqtruncat_claude PRIVATE -Wall -Wextra -Wpedantic)

qt_add_executable(aqtruncat-claude-metric
    providers/claude/claudeicon.h
    providers/claude/main.cpp)
target_link_libraries(aqtruncat-claude-metric PRIVATE aqtruncat_claude Qt6::Core)
target_compile_options(aqtruncat-claude-metric PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(aqtruncat-claude-metric PRIVATE AQTRUNCAT_VERSION="${PROJECT_VERSION}")
install(TARGETS aqtruncat-claude-metric RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# F3 Codex adapter: like the Claude Code one, a separate QtCore executable
# feeding F1 command-backed metrics ("output": "snapshot"); not linked by
# the core.
add_library(aqtruncat_codex STATIC
    providers/codex/codexusage.cpp
    providers/codex/codexusage.h)
target_include_directories(aqtruncat_codex PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/providers")
target_link_libraries(aqtruncat_codex PUBLIC aqtruncat_provider_common Qt6::Core)
target_compile_options(aqtruncat_codex PRIVATE -Wall -Wextra -Wpedantic)

qt_add_executable(aqtruncat-codex-metric
    providers/codex/codexicon.h
    providers/codex/main.cpp)
target_link_libraries(aqtruncat-codex-metric PRIVATE aqtruncat_codex Qt6::Core)
target_compile_options(aqtruncat-codex-metric PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(aqtruncat-codex-metric PRIVATE AQTRUNCAT_VERSION="${PROJECT_VERSION}")
install(TARGETS aqtruncat-codex-metric RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
