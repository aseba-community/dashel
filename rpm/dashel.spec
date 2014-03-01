%global commit 351e5768009605bf30b3045b865bdeb03faacd59
%global shortcommit %(c=%{commit}; echo ${c:0:7})
Name:           dashel
Version:        1.0.7
Release:        1.20140222git%{shortcommit}%{?dist}
Summary:        A C++ cross-platform data stream helper encapsulation library

License:        BSD
URL:            http://home.gna.org/dashel/
Source0:        https://github.com/aseba-community/dashel/archive/%{commit}/%{name}-%{version}-%{shortcommit}.tar.gz
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
* Sun Feb 23 2014  Dean Brettle <dean@brettle.com> 1.0.7-1.20140222git351e576
- Initial release
