# Build: packaging/make-source-tarball.sh ~/rpmbuild/SOURCES
#        rpmbuild -ba packaging/rpm/aqtruncat.spec
Name:           aqtruncat
Version:        1.5.6
Release:        1%{?dist}
Summary:        A Cute Run Cat for Linux

License:        Apache-2.0
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel >= 6.5
BuildRequires:  libarchive-devel
BuildRequires:  desktop-file-utils
BuildRequires:  dejavu-sans-fonts
Requires:       hicolor-icon-theme
# F4.1: the backend lives on the D-Bus session bus (activated on demand).
Requires:       dbus
Recommends:     qt6-qtwayland
# F4.2: the KDE Plasma 6 widget is plain QML, used only where Plasma is.
Suggests:       plasma-workspace
Suggests:       hwdata
Suggests:       flatpak
Suggests:       mangohud

%description
AQTRunCat is an animated runner in the system tray whose speed follows the
CPU load, with a Dashboard of system metrics, Custom Metrics from external
JSON producers and an optional Metrics Bar. It uses the generic Qt /
StatusNotifierItem tray and needs no desktop-specific components.

%prep
%autosetup
cp -p third_party/runcatneo/LICENSE LICENSE.RunCatNeo

%build
%cmake -G Ninja
%cmake_build

%install
%cmake_install
# Licenses go to %%{_licensedir} through %%license instead.
rm %{buildroot}%{_docdir}/%{name}/LICENSE %{buildroot}%{_docdir}/%{name}/LICENSE.RunCatNeo

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop
export QT_QPA_PLATFORM=offscreen
%ctest

%files
%license LICENSE LICENSE.RunCatNeo
%{_bindir}/%{name}
%{_bindir}/%{name}d
%{_bindir}/%{name}ctl
%{_datadir}/dbus-1/services/io.github.psykedady.AQTRunCat.service
%{_datadir}/%{name}/
%{_datadir}/plasma/plasmoids/io.github.psykedady.aqtruncat/
%{_bindir}/%{name}-claude-metric
%{_bindir}/%{name}-codex-metric
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg
%{_mandir}/man1/%{name}.1*
%{_docdir}/%{name}/

%changelog
* Thu Oct 22 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 1.5.6-1
- F5: built-in uptime, GPU, FPS (MangoHud log), package and Flatpak update
  counts (read-only, local metadata by default), kernel and distribution.

* Wed Oct 21 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 1.4.2-1
- F4.2: native KDE Plasma 6 widget (Plasmoid), a client of the backend;
  bundled runners also installed as files; GetRunners in the D-Bus API.

* Wed Oct 21 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 1.4.1-1
- F1-F4.1: Custom Metrics v2, Claude Code and Codex integrations, backend
  aqtruncatd with a D-Bus API and the aqtruncatctl command line.

* Fri Oct 09 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 0.0.1-1
- First packaged release.
