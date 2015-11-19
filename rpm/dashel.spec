Name:           dashel

# Update the following lines to reflect the source release version you will be
# referencing below
%global source_major 1
%global source_minor 1
%global source_patch 0
Version:        %{source_major}.%{source_minor}.%{source_patch}

# Update the following line with the git commit hash of the revision to use
# for example by running git show-ref -s --tags RELEASE_TAG
%global commit bd2781117e61cababbed36643024b0b3765d9f4b
%global shortcommit %(c=%{commit}; echo ${c:0:7})

# Update the following line to set commit_is_tagged_as_source_release to 0 if
# and only if the commit hash is not from a git tag for an existing source
# release (i.e. it is a commit hash for a pre-release or post-release
# revision). Otherwise set it to 1.
%global commit_is_tagged_as_source_release 1
%if %{commit_is_tagged_as_source_release} == 0
  %global snapshot .%(date +%%Y%%m%%d)git%{shortcommit}
%endif

# Update the number(s) in the "Release:" line below as follows. If this is 
# the first RPM release for a particular source release version, then set the
# number to 0. If this is the first RPM pre-release for a future source
# release version (i.e. the "Version:" line above refers to a future
# source release version), then set the number to 0.0. Otherwise, leave the
# the number unchanged. It will get bumped when you run rpmdev-bumpspec.
Release:        1%{?snapshot}%{?dist}

Summary:        A C++ cross-platform data stream helper encapsulation library

%global lib_pkg_name lib%{name}%{source_major}

%if 0%{?suse_version}
%global buildoutdir build
%else
%global buildoutdir .
%endif

%if 0%{?suse_version}
License:        BSD-3-Clause
%else
License:        BSD
%endif
URL:            http://home.gna.org/dashel/
Source0:        https://github.com/aseba-community/dashel/archive/%{commit}/%{name}-%{version}-%{commit}.tar.gz
Patch0:         dashel-rpm.patch

BuildRequires: binutils
BuildRequires: cmake
BuildRequires: elfutils
BuildRequires: file
BuildRequires: gdb
BuildRequires: glibc-devel
BuildRequires: kernel-headers
BuildRequires: libstdc++-devel
BuildRequires: libudev-devel
BuildRequires: doxygen
BuildRequires: gcc-c++

%description
No base package is installed.

%package -n %{lib_pkg_name}
Summary:        A C++ cross-platform data stream helper encapsulation library
Group: System/Libraries

%description  -n %{lib_pkg_name}
Dashel is a C++ cross-platform data stream helper encapsulation library. It
provides a unified access to TCP/UDP sockets, serial ports, console, and
files streams. It also allows a server application to wait for any activity
on any combination of these streams.

%package        devel
Summary:        Development files for %{name}
Requires:       %{lib_pkg_name}%{?_isa} = %{version}-%{release}
Group: Development/Libraries/C and C++

%description    devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{name}.


%prep
%setup -q -n %{name}-%{commit}
%patch0 -p1

%build
%cmake 
make %{?_smp_mflags}
doxygen %{_builddir}/%{buildsubdir}/Doxyfile

%install
rm -rf $RPM_BUILD_ROOT
cd %{buildoutdir}
make install DESTDIR=$RPM_BUILD_ROOT

%check
#ctest

%post -n %{lib_pkg_name}
/sbin/ldconfig

%postun -n %{lib_pkg_name}
/sbin/ldconfig


%files -n %{lib_pkg_name}
%doc readme.txt
%{_libdir}/*.so.*

%files devel
%doc readme.txt %{buildoutdir}/doc/* examples
%{_includedir}/*
%{_libdir}/*.so
%{_datadir}/dashel
%{_datadir}/dashel/*

%changelog
* Thu Nov 19 2015 Dean Brettle <dean@brettle.com> - 1.1.0-1
- Sync with upstream 1.1.0

* Wed Nov 18 2015 Dean Brettle <dean@brettle.com> - 1.0.9-2
- Sync with upstream 1.0.9

* Fri Jun 20 2014 Dean Brettle <dean@brettle.com> - 1.0.8-1.20140620git68d19e1
- Bumped minor version to sync with upstream.
- Added dashelConfig.cmake to -devel package.

* Mon May 05 2014 Dean Brettle <dean@brettle.com> - 1.0.7-9.20140505git8fb0d53
- Added EXTRA_LIBS to dashel's target_link_libraries so that it will link to
  libudev automatically.

* Mon May 05 2014 Dean Brettle <dean@brettle.com> - 1.0.7-8.20140505git8fb0d53
- Changed systemd-devel to libudev-devel in BuildRequires to accomodate
  OpenSUSE.

* Mon May 05 2014 Dean Brettle <dean@brettle.com> - 1.0.7-7.20140505git8fb0d53
- Added systemd-devel as build-requires to get libudev.h.

* Mon Apr 28 2014 Dean Brettle <dean@brettle.com> - 1.0.7-6
- Changed shortcommit to commit in Source0 to help OpenSUSE build server
  and started basing builds off of commit that is 1.0.7 + RPM fixes + version
  number fixes.

* Mon Mar 03 2014 Dean Brettle <dean@brettle.com> - 1.0.7-5
- Updated spec to run ldconfig when installing and uninstalling libdashel1
  packages

* Mon Mar 03 2014 Dean Brettle <dean@brettle.com> - 1.0.7-4
- Updated spec to build on openSUSE 13.1, RHEL 6, and CentOS 6 via Open Build
  Service.

* Sat Mar 01 2014 Dean Brettle <dean@brettle.com> - 1.0.7-3
- Rebased to official 1.0.7 commit.
- Added RPM building instructions.
- Changed SO_VERSION to SOVERSION in CMakeLists.txt.

* Thu Feb 27 2014 Dean Brettle <dean@brettle.com> - 1.0.7-2.140227git351e576
- Added instructions to dashel.spec

* Sun Feb 23 2014  Dean Brettle <dean@brettle.com> 1.0.7-1.20140222git351e576
- Initial release
