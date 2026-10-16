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

# Linux system metrics (CPU, memory, network, storage, disk I/O) from /proc,
# /sys and statvfs. Plain C++, no Qt; reuses core/cpustat for CPU usage.
# Shared by every consumer (CLI, Dashboard and Metrics Bar).
add_library(aqtruncat_metrics STATIC
    metrics/compactformat.cpp
    metrics/compactformat.h
    metrics/counterrates.h
    metrics/diskio.cpp
    metrics/diskio.h
    metrics/memory.cpp
    metrics/memory.h
    metrics/metricsformat.cpp
    metrics/metricsformat.h
    metrics/metricstatus.h
    metrics/network.cpp
    metrics/network.h
    metrics/storage.cpp
    metrics/storage.h
    metrics/systemmetrics.cpp
    metrics/systemmetrics.h
    metrics/textparse.cpp
    metrics/textparse.h)
target_link_libraries(aqtruncat_metrics PUBLIC aqtruncat_core)
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

# Desktop runtime and Widgets UI. MetricSampler is the sole metrics owner in
# the tray process; DashboardWindow and the runner are consumers.
add_library(aqtruncat_desktop STATIC
    custommetrics/commandmetric.cpp
    custommetrics/commandmetric.h
    custommetrics/custommetric.cpp
    custommetrics/custommetric.h
    app/applicationquit.cpp
    app/applicationquit.h
    app/appsettings.cpp
    app/appsettings.h
    app/autostartcommand.cpp
    app/autostartcommand.h
    app/autostartmanager.cpp
    app/autostartmanager.h
    app/cpumonitor.cpp
    app/cpumonitor.h
    app/custommetricpaths.cpp
    app/custommetricpaths.h
    app/custommetricsservice.cpp
    app/custommetricsservice.h
    app/customsymbol.cpp
    app/customsymbol.h
    app/dashboardwindow.cpp
    app/dashboardwindow.h
    app/metricsampler.cpp
    app/metricsbarmodel.cpp
    app/metricsbarmodel.h
    app/metricsbarpresenter.cpp
    app/metricsbarpresenter.h
    app/metricsbarrenderer.cpp
    app/metricsbarrenderer.h
    app/metricsampler.h
    app/runnerpresentation.cpp
    app/runnerpresentation.h
    app/runnerselection.cpp
    app/runnerselection.h
    app/runnerpreviewwidget.cpp
    app/runnerpreviewwidget.h
    app/runpreferences.cpp
    app/runpreferences.h
    app/runnerspeedcontroller.cpp
    app/runnerspeedcontroller.h
    app/settingswindow.cpp
    app/settingswindow.h
    app/snapshotthrottle.cpp
    app/snapshotthrottle.h
    app/storagepaths.cpp
    app/storagepaths.h
    app/traypresenter.cpp
    app/traypresenter.h)
target_link_libraries(aqtruncat_desktop PUBLIC aqtruncat_metrics aqtruncat_runner Qt6::Widgets)
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
# (upstream files, unmodified, see THIRD_PARTY_NOTICES.md) and AQTRunCat's own
# metadata.json from data/runners/<id>/.
set(AQTRUNCAT_BUNDLED_RUNNERS "cat;coffee" CACHE STRING "Runner ids shipped inside the executable")
set(AQTRUNCAT_DEFAULT_RUNNER "cat" CACHE STRING "Runner shown when none is requested")
if(NOT AQTRUNCAT_DEFAULT_RUNNER IN_LIST AQTRUNCAT_BUNDLED_RUNNERS)
    message(FATAL_ERROR "AQTRUNCAT_DEFAULT_RUNNER '${AQTRUNCAT_DEFAULT_RUNNER}' is not in AQTRUNCAT_BUNDLED_RUNNERS")
endif()
target_compile_definitions(aqtruncat PRIVATE
    AQTRUNCAT_VERSION="${PROJECT_VERSION}"
    AQTRUNCAT_DEFAULT_RUNNER="${AQTRUNCAT_DEFAULT_RUNNER}")

set(frames_root "${PROJECT_SOURCE_DIR}/third_party/runcatneo/runners")
set(metadata_root "${PROJECT_SOURCE_DIR}/data/runners")
set(bundled_frames "")
set(bundled_metadata "")
foreach(runner IN LISTS AQTRUNCAT_BUNDLED_RUNNERS)
    file(GLOB frames "${frames_root}/${runner}/*.png")
    if(NOT frames)
        message(FATAL_ERROR "bundled runner '${runner}' has no frames in ${frames_root}/${runner}")
    endif()
    list(APPEND bundled_frames ${frames})
    if(EXISTS "${metadata_root}/${runner}/metadata.json")
        list(APPEND bundled_metadata "${metadata_root}/${runner}/metadata.json")
    endif()
endforeach()
qt_add_resources(aqtruncat bundled_runner_frames PREFIX /runners BASE "${frames_root}" FILES ${bundled_frames})
if(bundled_metadata)
    qt_add_resources(aqtruncat bundled_runner_metadata PREFIX /runners BASE "${metadata_root}" FILES ${bundled_metadata})
endif()
