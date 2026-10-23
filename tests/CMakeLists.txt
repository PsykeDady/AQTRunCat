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
# F5 built-in metrics: uptime, kernel/distribution, GPU (fake sysfs, fake
# NVML), FPS (fake MangoHud logs) and update-check parsing.
foreach(name uptime systeminfo gpu fps packageupdates)
    qt_add_executable(tst_${name} tst_${name}.cpp)
    target_link_libraries(tst_${name} PRIVATE aqtruncat_metrics Qt6::Test)
    target_compile_options(tst_${name} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND tst_${name})
endforeach()
# F5 update checks with fake tools in a private PATH and a fake root.
qt_add_executable(tst_updatesservice tst_updatesservice.cpp)
target_link_libraries(tst_updatesservice PRIVATE aqtruncat_engine Qt6::Test)
target_compile_options(tst_updatesservice PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME updatesservice COMMAND tst_updatesservice)

# F7: the bundled runners as built and installed (real frames and metadata),
# the application's runner list, selection, persistence and fallbacks.
qt_add_executable(tst_bundledrunners tst_bundledrunners.cpp)
target_link_libraries(tst_bundledrunners PRIVATE aqtruncat_runner_catalog aqtruncat_engine Qt6::Test)
target_compile_options(tst_bundledrunners PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(tst_bundledrunners PRIVATE
    AQTRUNCAT_BUILD_RUNNERS="${PROJECT_BINARY_DIR}/share/aqtruncat/runners"
    AQTRUNCAT_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    AQTRUNCAT_APP="$<TARGET_FILE:aqtruncat>")
add_dependencies(tst_bundledrunners aqtruncat)
add_test(NAME bundledrunners COMMAND tst_bundledrunners)

# F6 hover summary of the Qt tray (CPU, RAM, swap, backend states).
qt_add_executable(tst_hoversummary tst_hoversummary.cpp)
target_link_libraries(tst_hoversummary PRIVATE aqtruncat_engine Qt6::Test)
target_compile_options(tst_hoversummary PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME hoversummary COMMAND tst_hoversummary)

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
target_link_libraries(tst_metricsbarmodel PRIVATE aqtruncat_desktop Qt6::Test Qt6::Widgets)
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
target_include_directories(tst_backendipc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
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

# F4.2: the KDE Plasma widget (plasmoid/package). Its pure functions with
# qmltestrunner; its D-Bus client against the build's backend, activated
# by a private bus; the package itself (metadata, structure, loading in
# plasmoidviewer when the Plasma SDK is there). Each part is registered
# only when its tools exist: AQTRunCat itself needs none of them.
set(plasmoid_package "${PROJECT_SOURCE_DIR}/plasmoid/package")
find_program(AQTRUNCAT_QMLTESTRUNNER NAMES qmltestrunner6 qmltestrunner
             HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBEXECS}")
if(AQTRUNCAT_QMLTESTRUNNER)
    add_test(NAME plasmoidlogic COMMAND ${AQTRUNCAT_QMLTESTRUNNER} -input ${CMAKE_CURRENT_SOURCE_DIR}/plasmoid/tst_logic.qml)
    set_tests_properties(plasmoidlogic PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
else()
    message(STATUS "qmltestrunner not found: the plasmoidlogic test is not registered")
endif()

find_package(Qt6 6.5 QUIET COMPONENTS Qml)
if(Qt6Qml_FOUND AND AQTRUNCAT_DBUS_RUN_SESSION)
    set(plasmoid_home "${CMAKE_CURRENT_BINARY_DIR}/plasmoidclient-home")
    set(plasmoid_bus_dir "${CMAKE_CURRENT_BINARY_DIR}/plasmoidclient-bus")
    # Activation through a wrapper that fails while the test's flag exists.
    file(GENERATE OUTPUT "${plasmoid_bus_dir}/aqtruncatd-wrapper"
         CONTENT "#!/bin/sh\n[ -e '${plasmoid_home}/activation-disabled' ] && exit 1\nexec '$<TARGET_FILE:aqtruncatd>' \"$@\"\n"
         FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
    file(WRITE "${plasmoid_bus_dir}/services/io.github.psykedady.AQTRunCat.service"
         "[D-BUS Service]\nName=io.github.psykedady.AQTRunCat\nExec=${plasmoid_bus_dir}/aqtruncatd-wrapper\n")
    file(WRITE "${plasmoid_bus_dir}/session.conf" "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"
 \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <auth>EXTERNAL</auth>
  <servicedir>${plasmoid_bus_dir}/services</servicedir>
  <policy context=\"default\">
    <allow send_destination=\"*\" eavesdrop=\"true\"/>
    <allow eavesdrop=\"true\"/>
    <allow own=\"*\"/>
  </policy>
</busconfig>
")
    qt_add_executable(tst_plasmoidclient tst_plasmoidclient.cpp)
    target_link_libraries(tst_plasmoidclient PRIVATE aqtruncat_api Qt6::Gui Qt6::Qml Qt6::Test Qt6::DBus)
    target_compile_options(tst_plasmoidclient PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(tst_plasmoidclient PRIVATE
        AQTRUNCAT_PLASMOID_PACKAGE="${plasmoid_package}"
        AQTRUNCAT_DAEMON="$<TARGET_FILE:aqtruncatd>"
        AQTRUNCAT_CTL="$<TARGET_FILE:aqtruncatctl>"
        AQTRUNCAT_TEST_HOME="${plasmoid_home}")
    add_dependencies(tst_plasmoidclient aqtruncatd aqtruncatctl)
    add_test(NAME plasmoidclient COMMAND ${AQTRUNCAT_DBUS_RUN_SESSION} --config-file=${plasmoid_bus_dir}/session.conf
             -- $<TARGET_FILE:tst_plasmoidclient>)
    # The bus (and so the activated backend) gets the test's home: never
    # the real user's settings, data, Claude Code or Codex directories.
    set_tests_properties(plasmoidclient PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;HOME=${plasmoid_home};XDG_CONFIG_HOME=${plasmoid_home}/config;XDG_DATA_HOME=${plasmoid_home}/data;XDG_CACHE_HOME=${plasmoid_home}/cache;XDG_STATE_HOME=${plasmoid_home}/state;XDG_RUNTIME_DIR=${plasmoid_home}/runtime;CLAUDE_CONFIG_DIR=${plasmoid_home}/no-claude;CODEX_HOME=${plasmoid_home}/no-codex")
else()
    message(STATUS "Qt Qml or dbus-run-session not found: the plasmoidclient test is not registered")
endif()

qt_add_executable(tst_plasmoidpackage tst_plasmoidpackage.cpp)
target_link_libraries(tst_plasmoidpackage PRIVATE Qt6::Core Qt6::Test)
target_compile_options(tst_plasmoidpackage PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(tst_plasmoidpackage PRIVATE
    AQTRUNCAT_PLASMOID_PACKAGE="${plasmoid_package}"
    AQTRUNCAT_DAEMON="$<TARGET_FILE:aqtruncatd>"
    AQTRUNCAT_TEST_VERSION="${PROJECT_VERSION}")
add_dependencies(tst_plasmoidpackage aqtruncatd)
if(AQTRUNCAT_DBUS_RUN_SESSION)
    # The same bus without service directories as backendipc.
    add_test(NAME plasmoidpackage COMMAND ${AQTRUNCAT_DBUS_RUN_SESSION} --config-file=${backendipc_bus}
             -- $<TARGET_FILE:tst_plasmoidpackage>)
else()
    add_test(NAME plasmoidpackage COMMAND tst_plasmoidpackage)
endif()
