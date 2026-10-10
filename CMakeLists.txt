cmake_minimum_required(VERSION 3.21)

project(AQTRunCat
    VERSION 0.0.1
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
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Test)
else()
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
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
# executable, so there are no runtime data files to install.
install(FILES data/aqtruncat.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/aqtruncat.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(DIRECTORY data/icons/hicolor DESTINATION ${CMAKE_INSTALL_DATADIR}/icons)
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES third_party/runcatneo/LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR} RENAME LICENSE.RunCatNeo)

# M1.0 tray spike: throwaway prototype kept for reference (docs/M1-tray-spike.md).
option(AQTRUNCAT_BUILD_SPIKES "Build the M1.0 tray spike" OFF)
if(AQTRUNCAT_BUILD_SPIKES)
    add_subdirectory(spike/m1-tray)
endif()
