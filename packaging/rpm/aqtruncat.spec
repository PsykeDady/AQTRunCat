# Build: packaging/make-source-tarball.sh ~/rpmbuild/SOURCES
#        rpmbuild -ba packaging/rpm/aqtruncat.spec
Name:           aqtruncat
Version:        2.3.0
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
desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}-settings.desktop
export QT_QPA_PLATFORM=offscreen
%ctest

%files
%license LICENSE LICENSE.RunCatNeo
%{_bindir}/%{name}
%{_bindir}/%{name}d
%{_bindir}/%{name}ctl
%{_datadir}/dbus-1/services/io.github.psykedady.AQTRunCat.service
%{_datadir}/dbus-1/services/io.github.psykedady.AQTRunCat.Settings.service
%{_datadir}/%{name}/
%{_datadir}/plasma/plasmoids/io.github.psykedady.aqtruncat/
%{_bindir}/%{name}-claude-metric
%{_bindir}/%{name}-codex-metric
%{_datadir}/applications/%{name}.desktop
%{_datadir}/applications/%{name}-settings.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg
%{_mandir}/man1/%{name}.1*
%{_docdir}/%{name}/

%changelog
* Tue Oct 27 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 2.3.0-1
- Initialize v2F3 Dependency Advisor & Hardware Monitoring; no functional changes.

* Tue Oct 27 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 2.2.0-1
- Standalone Settings via aqtruncat settings, a dedicated application-menu
  entry and tray/Plasmoid activation; D-Bus backend activation is unchanged.
- Clarify that login autostart starts the Qt Tray Plugin.

* Tue Oct 27 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 2.1.8-1
- 2.1.8: Discover Custom Metrics recursively through organisational
  directories. A directory containing metric.json remains a package boundary;
  only source.type "group" gives its subdirectories group semantics.

* Sat Oct 24 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 2.1.7-1
- 2.1.7 (v2F1): Manual refresh of Custom Metrics: clicking a metric's icon
  in the Dashboard or the Plasma widget (a group's: all its enabled
  metrics), a Metrics Bar item's middle click or "Refresh Now", and
  "aqtruncatctl custom refresh" sample it now through the backend's
  scheduler; a running metric is never started twice. Completes v2F1.

* Sat Oct 24 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 2.1.6-1
- 2.1.6 (v2F1): Reload Custom Metrics without restarting: Settings, the
  Plasma widget and "aqtruncatctl custom reload" rescan the files and apply
  the metrics and groups that were added, changed or removed; unchanged
  commands keep running, a reload that cannot read a directory changes
  nothing.

* Fri Oct 23 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 2.1.5-1
- 2.1.5 (v2F1): Custom Metric groups: a package's metric.json with
  "source": {"type": "group"} whose subdirectories are child metrics that
  inherit its configuration; shown as one compact card in the Dashboard and
  the Plasma widget, managed as a tree in Settings.

* Fri Oct 23 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 2.1.4-1
- 2.1.4 (v2F1): optional display.showBar for Custom Metrics: false hides
  their progress bars in the Dashboard and the Plasma widget.

* Fri Oct 23 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 2.1.3-1
- 2.1.3 (v2F1): Custom Metric execution warnings: a failing command
  (non-zero exit other than 69, missing or non-executable program, launch
  failure, timeout, invalid output) is an error with its reason in the
  Dashboard, Metrics Bar, Plasma widget and Settings, never a stale value.

* Fri Oct 23 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 2.1.2-1
- 2.1.2 (v2F1): optional "enabled" flag for Custom Metrics, with a
  checkbox in Settings and in the Plasma widget's configuration; the Plasma
  popup keeps its scroll position and no longer redraws a Custom Metric
  while its command runs again; the Dashboard
  only redraws the Custom Metrics cards that changed.

* Fri Oct 23 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 2.1.1-1
- 2.1.1 (v2F1, includes the 2.1.0 baseline): Rickroll built-in runner.

* Thu Oct 22 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 2.0.0-1
- 2.0.0: consolidation release of the complete 1.x application (1.7.4);
  documentation reorganised by topic, v2 roadmap. No functional changes.

* Thu Oct 22 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 1.7.4-1
- F7: Walking Goose, Party Parrot and Engine built-in runners.

* Thu Oct 22 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 1.6.3-1
- F6: hover information: CPU, RAM and swap summary in the tray tooltip and
  in the Plasma widget's tooltip; more details in the Metrics Bar tooltips.

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
