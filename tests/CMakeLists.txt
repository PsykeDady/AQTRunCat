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

qt_add_executable(tst_claudecodeintegration tst_claudecodeintegration.cpp)
target_link_libraries(tst_claudecodeintegration PRIVATE aqtruncat_desktop Qt6::Test Qt6::Widgets)
target_compile_options(tst_claudecodeintegration PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(tst_claudecodeintegration PRIVATE
    AQTRUNCAT_CLAUDE_ADAPTER="$<TARGET_FILE:aqtruncat-claude-metric>")
add_dependencies(tst_claudecodeintegration aqtruncat-claude-metric)
add_test(NAME claudecodeintegration COMMAND tst_claudecodeintegration)
set_tests_properties(claudecodeintegration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
