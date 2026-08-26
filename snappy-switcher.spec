# RPM Spec File - Snappy Switcher v3.2.0
# For Fedora Copr / RHEL / openSUSE

Name:           snappy-switcher
Version:        4.5.0
Release:        1%{?dist}
Summary:        A fast, animated Alt+Tab window switcher for Hyprland

License:        GPL-3.0-or-later
URL:            https://github.com/OpalAayan/snappy-switcher
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

# Build dependencies
BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  pkgconf-pkg-config
BuildRequires:  wayland-devel
BuildRequires:  wayland-protocols-devel
BuildRequires:  cairo-devel
BuildRequires:  pango-devel
BuildRequires:  libxkbcommon-devel
BuildRequires:  glib2-devel
BuildRequires:  json-c-devel
BuildRequires:  librsvg2-devel


# Recommended (not hard requirements)
Recommends:     hyprland

%description
Snappy Switcher is a lightweight, fast Alt+Tab window switcher designed for
Hyprland. It features smooth animations, MRU (Most Recently Used) window
sorting, context-based grouping, and full theme customization with built-in
support for Catppuccin, Nord, Dracula, and more.

%prep
%autosetup -n %{name}-%{version}

%build
%make_build PREFIX=%{_prefix}

%install
# Binaries
install -Dpm 755 snappy-switcher %{buildroot}%{_bindir}/snappy-switcher
install -Dpm 755 scripts/snappy-wrapper.sh %{buildroot}%{_bindir}/snappy-wrapper
install -Dpm 755 scripts/install-config.sh %{buildroot}%{_bindir}/snappy-install-config

# Themes
install -d %{buildroot}%{_datadir}/%{name}/themes
install -pm 644 themes/*.ini %{buildroot}%{_datadir}/%{name}/themes/

# System config defaults (XDG)
install -Dpm 644 config.ini.example %{buildroot}%{_sysconfdir}/xdg/%{name}/config.ini

# Documentation
install -Dpm 644 README.md %{buildroot}%{_docdir}/%{name}/README.md
install -Dpm 644 docs/ARCHITECTURE.md %{buildroot}%{_docdir}/%{name}/ARCHITECTURE.md
install -Dpm 644 docs/CONFIGURATION.md %{buildroot}%{_docdir}/%{name}/CONFIGURATION.md
install -Dpm 644 config.ini.example %{buildroot}%{_docdir}/%{name}/config.ini.example

# Systemd user service
install -Dpm 644 snappy-switcher.service %{buildroot}%{_userunitdir}/snappy-switcher.service

%files
# Binaries
%{_bindir}/snappy-switcher
%{_bindir}/snappy-wrapper
%{_bindir}/snappy-install-config

# Data (themes)
%dir %{_datadir}/%{name}
%dir %{_datadir}/%{name}/themes
%{_datadir}/%{name}/themes/*.ini

# System config
%dir %{_sysconfdir}/xdg/%{name}
%config(noreplace) %{_sysconfdir}/xdg/%{name}/config.ini

# Documentation
%dir %{_docdir}/%{name}
%doc %{_docdir}/%{name}/README.md
%doc %{_docdir}/%{name}/ARCHITECTURE.md
%doc %{_docdir}/%{name}/CONFIGURATION.md
%doc %{_docdir}/%{name}/config.ini.example

# Systemd user service
%{_userunitdir}/snappy-switcher.service

%changelog
* Wed Jun 03 2026 OpalAayan <YougurtMyFace@proton.me> - 4.5.0
- Added support for latest/Old Hyprland
- Fixed Dispatch
- Made this Alt Tab switcher to window switcher
- did something cant remember 

