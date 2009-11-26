
 # spec file for package PackageName (Version PackageVersion)
 # Copyright 2009 SUSE LINUX Products GmbH, Nuernberg, Germany.
 # Copyright Johan Persson <johan162@gmail.com>". 
 #
 # All modifications and additions to the file contributed by third parties
 # remain the property of their copyright owners, unless otherwise agreed
 # upon. The license for this file, and modifications and additions to the
 # file, is the same license as for the pristine package itself (unless the
 # license for the pristine package is not an Open Source License, in which
 # case the license is the MIT License). An "Open Source License" is a
 # license that conforms to the Open Source Definition (Version 1.9)
 # published by the Open Source Initiative.

BuildRequires: v4l-tools glibc-devel libiniparser-devel libxml2-devel pcre-devel
Summary: tvpvrd - TV Personal Video Recorder Daemon
Name: tvpvrd
Version: 1.0.1
Release: 1.1
License: GPLv3
Group: Multimedia/Vide

# main source bundle
Source0: %{name}-%{version}.tar.gz

%description
tvpvrd is aflexible and configurable server daemon that acts as an advanced 
digital TV recorder using one or several installed TV capture cards.
The server manages scheduled recordings and provides an efficient command language 
interface on a dedicated TCP/IP port.

The goal is to provide an advanced recording and transcoding (using ffmpeg) server 
without the need to have to configure databases or a complex GUI.

# ---------------------------------------------------------------------------------
# PREPARE
# Extract original source tar ball
%prep
%setup -q

# ---------------------------------------------------------------------------------
# BUILD 
# configure and build. The %configure macro will automatically set the
# correct prefix and sysconfdir directories
%build
autoreconf -fi 
%configure
make

# ---------------------------------------------------------------------------------
# INSTALL 
# The %makeinstall macro will make a install into the proper staging
# directory used during the RPM build
%install
%makeinstall

# ---------------------------------------------------------------------------------
# FILES
# 
%files
%defattr(-,root,root) 
/usr/bin/tvpvrd
/usr/bin/stop-tvpvrd
/etc/tvpvrd

%doc %attr(0444,root,root) /usr/share/man/man1/tvpvrd.1.gz
%doc COPYING AUTHORS README NEWS 
%doc docs/manpages/tvpvrd.1.pdf
%doc docs/manpages/tvpvrd.1.html

%clean
%__rm -rf %{buildroot}

# ---------------------------------------------------------------------------------
# Setup the user that normally will run tvpvrd
%pre  
/usr/sbin/useradd -r -g users -s /bin/false -c "tvpvrd daemon" tvpvrd > /dev/null || :  
test -e /var/run/tvpvrd.pid  || rm -rf /var/run/tvpvrd.pid && :  

%preun
# ---------------------------------------------------------------------------------
# Setup the user that normally will run tvpvrd
/usr/sbin/userdel tvpvrd > /dev/null ||
test -e /var/run/tvpvrd.pid  || rm -rf /var/run/tvpvrd.pid && : 

%changelog
* Thu Nov 26 2009  Johan Persson  <johan162@gmail.com>
- Maintenance release 1.0.1
- Updated build structure with RPM building capability
- Updated source to build clean even with FORTIFY_SOURCE=2 flag
- Corrected missing file in 1.0.0 tar package build
 

