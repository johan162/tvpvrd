 # spec file for package tvpvrd (Version 1.0.3)
 # Copyright Johan Persson <johan162@gmail.com> 
 #
 # All modifications and additions to the file contributed by third parties
 # remain the property of their copyright owners, unless otherwise agreed
 # upon. The license for this file, and modifications and additions to the
 # file, is the same license as for the pristine package itself (unless the
 # license for the pristine package is not an Open Source License, in which
 # case the license is the MIT License). An "Open Source License" is a
 # license that conforms to the Open Source Definition (Version 1.9)
 # published by the Open Source Initiative.

BuildRequires:  v4l-tools glibc-devel libiniparser-devel libxml2-devel pcre-devel libxslt docbook-xsl-stylesheets readline-devel
PreReq:         pwdutils coreutils
Summary:        TV Personal Video Recorder Daemon
Name:           tvpvrd
Version:        3.1.1
Release:        1.1
License:        GPLv3
Group:          Productivity/Multimedia/Other
BuildRoot:      %{_tmppath}/%{name}-%{version}-build  
AutoReqProv:    on 
URL:            http://sourceforge.net/projects/tvpvrd

# main source bundle
Source0: https://downloads.sourceforge.net/project/tvpvrd/%{name}-%{version}.tar.bz2

%description
TV Personal Video recorder daemon. Schedule and manage video recordings from one or more
TV capture cards, e.g. Hauppauge 150, 250, or 350. The server also provides automatic 
transcoding of recordings via ffmpeg, for example, MP4 format.

Authors:
Johan Persson <johan162@gmail.com>

# ---------------------------------------------------------------------------------
# PREPARE
# Extract original source tar ball and configure the build
# ---------------------------------------------------------------------------------
%prep
%setup -q
%configure

# ---------------------------------------------------------------------------------
# BUILD 
# configure and build. The configure macro will automatically set the
# correct prefix and sysconfdir directories the smp macro will make sure the
# build is parallelized
# ---------------------------------------------------------------------------------
%build
make %{?_smp_mflags}

# ---------------------------------------------------------------------------------
# INSTALL 
# The makeinstall macro will make a install into the proper staging
# directory used during the RPM build
# ---------------------------------------------------------------------------------
%install
%makeinstall

mkdir -p %{buildroot}%{_sbindir}
ln -sf %{_initrddir}/tvpvrd %{buildroot}%{_sbindir}/rctvpvrd

# Properly setup the init.d script so that the daemon is started in the right
# runlevels according to LSB header in the init.d file
# -f = skip the fillup part
%fillup_and_insserv -f 

# ---------------------------------------------------------------------------------
# Clean up after build
# ---------------------------------------------------------------------------------
%clean
%__rm -rf %{buildroot}

# ---------------------------------------------------------------------------------
# Setup the user that normally will run tvpvrd
# ---------------------------------------------------------------------------------
%post  
/usr/sbin/groupadd -r tvpvrd 2> /dev/null || : 
/usr/sbin/useradd -r -g tvpvrd -s /bin/false -c "tvpvrd daemon" tvpvrd 2> /dev/null || : 
/usr/sbin/usermod -g tvpvrd tvpvrd 2>/dev/null || : 
test -e /var/run/tvpvrd.pid  || rm -rf /var/run/tvpvrd.pid && :

# ---------------------------------------------------------------------------------
# Stop the service before removing
# ---------------------------------------------------------------------------------
%preun
%stop_on_removal tvpvrd

# ---------------------------------------------------------------------------------
# Cleanup after install
# ---------------------------------------------------------------------------------
%postun
%insserv_cleanup
test -e /var/run/tvpvrd.pid  || rm -rf /var/run/tvpvrd.pid && : 

# ---------------------------------------------------------------------------------
# FILES
# ---------------------------------------------------------------------------------
%files
%defattr(-,root,root) 
%{_bindir}/tvpvrd
%{_bindir}/tvpsh
%{_initrddir}/tvpvrd
%{_sbindir}/rctvpvrd
%config %{_sysconfdir}/tvpvrd
%doc %attr(0444,root,root) %{_mandir}/man1/tvpvrd.1*
%doc %attr(0444,root,root) %{_mandir}/man1/tvpowerd.1*
%doc %attr(0444,root,root) %{_mandir}/man1/tvpsh.1*
%doc COPYING AUTHORS README NEWS 
%doc docs/manpages/tvpvrd.1.pdf
%doc docs/manpages/tvpvrd.1.html
%doc docs/manpages/tvpowerd.1.pdf
%doc docs/manpages/tvpowerd.1.html
%doc docs/manpages/tvpsh.1.pdf
%doc docs/manpages/tvpsh.1.html

# ---------------------------------------------------------------------------------
# CHANGELOG
# ---------------------------------------------------------------------------------
%changelog
* Wed May 4 2011 Johan Persson <johan162@gmail.com> - 3.1.1-16.1
- Cleanup of RPM spec file 