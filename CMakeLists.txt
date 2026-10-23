cmake_minimum_required(VERSION 3.21)

project(AQTRunCat
    VERSION 2.1.3
    DESCRIPTION "A Cute Run Cat for Linux"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Install layout: standard GNU directories under CMAKE_INSTALL_PREFIX. The
# documentation directory follows the executable/package name.
if(NOT DEFINED CMAKE_INSTALL_DOCDIR)
    set(CMAKE_INSTALL_DOCDIR "share/doc/aqtruncat" CACHE PATH "Documentation root (DOCDIR)")
endif()
include(GNUInstallDirs)

option(BUILD_TESTING "Build the automated test suite" ON)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets DBus Test)
else()
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets DBus)
endif()

# Runner ZIP import (aqtruncat runner import).
find_package(LibArchive 3.0)
if(NOT LibArchive_FOUND)
    message(FATAL_ERROR
        "libarchive (>= 3.0) development files not found; they are needed to import runner ZIP archives.\n"
        "Install them, e.g. Arch: libarchive, Debian/Ubuntu: libarchive-dev, Fedora: libarchive-devel,\n"
        "or point CMake at them with -DLibArchive_INCLUDE_DIR=... -DLibArchive_LIBRARY=...")
endif()
qt_standard_project_setup()
# qt_standard_project_setup() gives installed binaries a relocatable
# $ORIGIN RUNPATH. AQTRunCat installs no libraries of its own, so keep a
# RUNPATH only for link directories outside the system ones (a Qt installed
# by hand); distribution builds against system Qt get none.
set(CMAKE_INSTALL_RPATH "")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

add_subdirectory(src)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Desktop integration and documentation. Bundled runners are compiled into the
# executable; since F4.2 they are also installed as files (src/CMakeLists.txt)
# for frontends outside it.
install(FILES data/aqtruncat.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/aqtruncat.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
# F4.1: D-Bus activation of the backend on the session bus (found through
# $XDG_DATA_DIRS / $XDG_DATA_HOME by dbus-daemon and dbus-broker).
set(AQTRUNCAT_DBUS_EXEC "${CMAKE_INSTALL_FULL_BINDIR}/aqtruncatd")
configure_file(data/io.github.psykedady.AQTRunCat.service.in
               ${CMAKE_CURRENT_BINARY_DIR}/io.github.psykedady.AQTRunCat.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.psykedady.AQTRunCat.service
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(DIRECTORY data/icons/hicolor DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)
# F4.2: the KDE Plasma 6 widget, a QML-only Plasma package (nothing to
# build, no KDE build dependency). Plasma finds it through XDG_DATA_DIRS;
# install.sh leaves it out without Plasma 6 or with --no-plasmoid.
option(AQTRUNCAT_INSTALL_PLASMOID "Install the KDE Plasma 6 widget (QML files only)" ON)
if(AQTRUNCAT_INSTALL_PLASMOID)
    install(DIRECTORY plasmoid/package/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/plasma/plasmoids/io.github.psykedady.aqtruncat)
endif()
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES third_party/runcatneo/LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME LICENSE.RunCatNeo)

# M1.0 tray spike: throwaway prototype kept for reference (docs/tray-spike.md).
option(AQTRUNCAT_BUILD_SPIKES "Build the M1.0 tray spike" OFF)
if(AQTRUNCAT_BUILD_SPIKES)
    add_subdirectory(spike/m1-tray)
endif()
