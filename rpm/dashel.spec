Name:           dashel
# Update the following line to reflect the source release version you will be
# referencing below
Version:        1.0.7

# Update the following line with the git commit hash of the revision to use
# for example by running git show-ref -s --tags RELEASE_TAG
%global commit c706942dce2f2df9caf6d12bc851c68b92f8b64f
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
Release:        3%{?snapshot}%{?dist}

Summary:        A C++ cross-platform data stream helper encapsulation library
License:        BSD
URL:            http://home.gna.org/dashel/
Source0:        https://github.com/aseba-community/dashel/archive/%{commit}/%{name}-%{version}-%{shortcommit}.tar.gz
Patch0:         dashel-rpm.patch

BuildRequires: binutils
BuildRequires: cmake
BuildRequires: dwz
BuildRequires: elfutils
BuildRequires: file
BuildRequires: gdb
BuildRequires: glibc-devel
BuildRequires: glibc-headers
BuildRequires: kernel-headers
BuildRequires: libstdc++-devel
BuildRequires: systemd-devel
BuildRequires: doxygen

%description
Dashel is a C++ cross-platform data stream helper encapsulation library. It
provides a unified access to TCP/UDP sockets, serial ports, console, and
files streams. It also allows a server application to wait for any activity
on any combination of these streams.

%package        devel
Summary:        Development files for %{name}
Requires:       %{name}%{?_isa} = %{version}-%{release}

%description    devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{name}.


%prep
%setup -q -n %{name}-%{commit}
%patch0 -p1

%build
%cmake .
make %{?_smp_mflags}
doxygen

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

%check
#ctest

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig


%files
%doc readme.txt
%{_libdir}/*.so.*

%files devel
%doc readme.txt doc/* examples
%{_includedir}/*
%{_libdir}/*.so


%changelog
* Sat Mar 01 2014 Dean Brettle <dean@brettle.com> - 1.0.7-3
- Rebased to official 1.0.7 commit.
- Added RPM building instructions.
- Changed SO_VERSION to SOVERSION in CMakeLists.txt.

* Thu Feb 27 2014 Dean Brettle <dean@brettle.com> - 1.0.7-2.140227git351e576
- Added instructions to dashel.spec

* Sun Feb 23 2014  Dean Brettle <dean@brettle.com> 1.0.7-1.20140222git351e576
- Initial release
