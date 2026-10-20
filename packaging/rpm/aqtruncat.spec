# Build: packaging/make-source-tarball.sh ~/rpmbuild/SOURCES
#        rpmbuild -ba packaging/rpm/aqtruncat.spec
Name:           aqtruncat
Version:        0.0.1
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
Recommends:     qt6-qtwayland

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
%{_bindir}/%{name}-claude-metric
%{_bindir}/%{name}-codex-metric
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg
%{_mandir}/man1/%{name}.1*
%{_docdir}/%{name}/

%changelog
* Fri Oct 09 2026 AQTRunCat maintainers <maintainers@aqtruncat.invalid> - 0.0.1-1
- First packaged release.
