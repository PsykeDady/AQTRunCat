# Qt-free core: CPU, smoothing, speed curve, frame playback.
foreach(name cpustat speedcurve smoothing frameplayback)
    qt_add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE aqtruncat_core Qt6::Test)
    target_compile_options(tst_${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND tst_${name})
endforeach()

# Runner system: names/ordering, loader, discovery/selection, animator.
foreach(name framenames runnerloader runnercatalog runneranimator)
    qt_add_executable(tst_${name} tst_${name}.cpp runnerfixture.h)
    target_link_libraries(tst_${name} PRIVATE aqtruncat_runner Qt6::Test)
    target_compile_options(tst_${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND tst_${name})
endforeach()

# Runner lifecycle (archive import, install, remove) and command line.
foreach(name runnerarchive runnerlifecycle cli)
    qt_add_executable(tst_${name} tst_${name}.cpp runnerfixture.h archivefixture.h)
    target_link_libraries(tst_${name} PRIVATE aqtruncat_cli aqtruncat_runner LibArchive::LibArchive Qt6::Test)
    target_compile_options(tst_${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND tst_${name})
endforeach()

# System metrics: parsers and rate logic (fixtures, never the real /proc),
# formatting, and the fixtures → snapshot → CLI report flow.
foreach(name memory network storage diskio metricsformat compactformat)
    qt_add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE aqtruncat_metrics Qt6::Test)
    target_compile_options(tst_${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND tst_${name})
endforeach()
qt_add_executable(tst_systemmetrics tst_systemmetrics.cpp)
target_link_libraries(tst_systemmetrics PRIVATE aqtruncat_cli aqtruncat_metrics Qt6::Test)
target_compile_options(tst_systemmetrics PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME systemmetrics COMMAND tst_systemmetrics)

# M4 desktop shared sampler, Root/Home selection and Widgets Dashboard.
foreach(name desktopmetrics storagepaths dashboard)
    qt_add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE aqtruncat_desktop aqtruncat_metrics Qt6::Test Qt6::Widgets)
    target_compile_options(tst_${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND tst_${name})
endforeach()
set_tests_properties(dashboard PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# M5 typed persistence, desktop runtime policy, preview and Settings UI.
qt_add_executable(tst_appsettings tst_appsettings.cpp)
target_link_libraries(tst_appsettings PRIVATE aqtruncat_desktop Qt6::Test)
target_compile_options(tst_appsettings PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME appsettings COMMAND tst_appsettings)

qt_add_executable(tst_desktopsettings tst_desktopsettings.cpp runnerfixture.h)
target_link_libraries(tst_desktopsettings PRIVATE aqtruncat_desktop aqtruncat_runner Qt6::Test)
target_compile_options(tst_desktopsettings PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME desktopsettings COMMAND tst_desktopsettings)

foreach(name runnerpreview settingswindow)
    qt_add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE aqtruncat_desktop Qt6::Test Qt6::Widgets)
    target_compile_options(tst_${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND tst_${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# M6 RunCat Neo-compatible JSON parsing and application-owned registry/watcher.
foreach(name custommetric custommetricsservice)
    qt_add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE aqtruncat_desktop Qt6::Test)
    target_compile_options(tst_${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND tst_${name})
endforeach()

# F1 command-backed Custom Metrics: deterministic helper plus parser/scheduler
# tests. The helper is invoked directly through QProcess; no shell or host
# utility participates in the test contract.
add_executable(custommetric_test_helper custommetric_test_helper.cpp)
target_compile_options(custommetric_test_helper PRIVATE -Wall -Wextra -Wpedantic)
foreach(name commandmetric commandmetricsservice)
    qt_add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE aqtruncat_desktop Qt6::Test)
    target_compile_options(tst_${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_dependencies(tst_${name} custommetric_test_helper)
    add_test(NAME ${name} COMMAND tst_${name})
endforeach()

# M7 Metrics Bar: semantic model, square SNI rendering and tray item lifecycle.
qt_add_executable(tst_metricsbarmodel tst_metricsbarmodel.cpp)
target_link_libraries(tst_metricsbarmodel PRIVATE aqtruncat_desktop Qt6::Test)
target_compile_options(tst_metricsbarmodel PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME metricsbarmodel COMMAND tst_metricsbarmodel)

foreach(name metricsbarrenderer metricsbarpresenter)
    qt_add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE aqtruncat_desktop Qt6::Test Qt6::Widgets)
    target_compile_options(tst_${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND tst_${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# M9 installer: command line, distribution detection, XDG paths and a real
# install/reinstall/uninstall cycle into temporary directories (never /usr).
find_program(AQTRUNCAT_BASH bash)
if(AQTRUNCAT_BASH)
    add_test(NAME installer
             COMMAND ${AQTRUNCAT_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/installer/test_install.sh
                     ${PROJECT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
    # It builds (and may relink) this very build tree through --build-dir:
    # never at the same time as tests that are running its executables.
    set_tests_properties(installer PROPERTIES RUN_SERIAL TRUE)
endif()
if(AQTRUNCAT_BASH)
    add_test(NAME packaging-versions
             COMMAND ${AQTRUNCAT_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/packaging/check_versions.sh ${PROJECT_SOURCE_DIR})
endif()

# F2 Claude Code adapter: synthetic Claude Code state and a fake /proc for the
# provider logic, then the real adapter executable run through F1 into the
# Dashboard and Metrics Bar. Never the developer's ~/.claude, never the network.
qt_add_executable(tst_claudecodemetric tst_claudecodemetric.cpp)
target_link_libraries(tst_claudecodemetric PRIVATE aqtruncat_claude Qt6::Test)
target_compile_options(tst_claudecodemetric PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME claudecodemetric COMMAND tst_claudecodemetric)

# F3 Codex adapter: synthetic Codex homes and a fake "codex" app-server.
qt_add_executable(tst_codexusage tst_codexusage.cpp)
target_link_libraries(tst_codexusage PRIVATE aqtruncat_codex Qt6::Test)
target_compile_options(tst_codexusage PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME codexusage COMMAND tst_codexusage)

foreach(name claudeusage claudestatusline)
    qt_add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE aqtruncat_claude Qt6::Test)
    target_compile_options(tst_${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND tst_${name})
endforeach()

qt_add_executable(tst_claudecodeintegration tst_claudecodeintegration.cpp)
target_link_libraries(tst_claudecodeintegration PRIVATE aqtruncat_desktop Qt6::Test Qt6::Widgets)
target_compile_options(tst_claudecodeintegration PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(tst_claudecodeintegration PRIVATE
    AQTRUNCAT_CLAUDE_ADAPTER="$<TARGET_FILE:aqtruncat-claude-metric>")
add_dependencies(tst_claudecodeintegration aqtruncat-claude-metric)
add_test(NAME claudecodeintegration COMMAND tst_claudecodeintegration)
set_tests_properties(claudecodeintegration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_codexintegration tst_codexintegration.cpp)
target_link_libraries(tst_codexintegration PRIVATE aqtruncat_desktop Qt6::Test Qt6::Widgets)
target_compile_options(tst_codexintegration PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(tst_codexintegration PRIVATE
    AQTRUNCAT_CODEX_ADAPTER="$<TARGET_FILE:aqtruncat-codex-metric>"
    AQTRUNCAT_CLAUDE_ADAPTER="$<TARGET_FILE:aqtruncat-claude-metric>")
add_dependencies(tst_codexintegration aqtruncat-codex-metric aqtruncat-claude-metric)
add_test(NAME codexintegration COMMAND tst_codexintegration)
set_tests_properties(codexintegration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# F4.1: API schema (no D-Bus).
qt_add_executable(tst_apischema tst_apischema.cpp)
target_link_libraries(tst_apischema PRIVATE aqtruncat_api Qt6::Test)
target_compile_options(tst_apischema PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME apischema COMMAND tst_apischema)

# F4.1: backend, API, frontend client and CLI end to end, on an isolated
# session bus (dbus-run-session) with temporary XDG/home directories.
find_program(AQTRUNCAT_DBUS_RUN_SESSION dbus-run-session)
qt_add_executable(tst_backendipc tst_backendipc.cpp)
target_link_libraries(tst_backendipc PRIVATE aqtruncat_desktop Qt6::Test Qt6::Widgets Qt6::DBus)
target_compile_options(tst_backendipc PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(tst_backendipc PRIVATE
    AQTRUNCAT_DAEMON="$<TARGET_FILE:aqtruncatd>"
    AQTRUNCAT_CTL="$<TARGET_FILE:aqtruncatctl>"
    AQTRUNCAT_TEST_VERSION="${PROJECT_VERSION}")
add_dependencies(tst_backendipc aqtruncatd aqtruncatctl aqtruncat-claude-metric aqtruncat-codex-metric)
if(AQTRUNCAT_DBUS_RUN_SESSION)
    # A session bus without service directories: an AQTRunCat installed on
    # this machine (share/dbus-1/services) must not be activated by the test.
    set(backendipc_bus "${CMAKE_CURRENT_BINARY_DIR}/backendipc-bus.conf")
    file(WRITE "${backendipc_bus}" [=[<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
]=])
    add_test(NAME backendipc COMMAND ${AQTRUNCAT_DBUS_RUN_SESSION} --config-file=${backendipc_bus} -- $<TARGET_FILE:tst_backendipc>)
    set_tests_properties(backendipc PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
else()
    message(STATUS "dbus-run-session not found: the backendipc test is not registered")
endif()
