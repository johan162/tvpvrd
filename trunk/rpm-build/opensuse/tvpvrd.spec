 # spec file for package tvpvrd (Version 1.0.2)
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

BuildRequires:  v4l-tools glibc-devel libiniparser-devel libxml2-devel pcre-devel libxslt docbook-xsl-stylesheets
PreReq:         pwdutils coreutils
Summary:        TV Personal Video Recorder Daemon
Name:           tvpvrd
Version:        1.0.2
Release:        1.1
License:        GPLv3
Group:          Productivity/Multimedia/Other
BuildRoot:      %{_tmppath}/%{name}-%{version}-build  
AutoReqProv:    on 
Packager:       Johan Persson <johan162@gmail.com>

# main source bundle
Source0: %{name}-%{version}.tar.bz2

%description
TV Personal Video recorder daemon. Schedule and manage video recordings from one or more
TV capture cards, e.g. Hauppauge 150, 250, or 350. The server also provides automatic 
transcoding of recordings via ffmpeg, for example, MP4 format.

Authors:
Johan Persson <johan162@gmail.com>

# ---------------------------------------------------------------------------------
# PREPARE
# Extract original source tar ball
# ---------------------------------------------------------------------------------
%prep
%setup -q

# ---------------------------------------------------------------------------------
# BUILD 
# configure and build. The %configure macro will automatically set the
# correct prefix and sysconfdir directories
%build
%configure
make

# ---------------------------------------------------------------------------------
# INSTALL 
# The %makeinstall macro will make a install into the proper staging
# directory used during the RPM build
# ---------------------------------------------------------------------------------
%install
%makeinstall
install -d $RPM_BUILD_ROOT/%{_sbindir}
#ln -sf $RPM_BUILD_ROOT%{_sysconfdir}/init.d/tvpvrd  $RPM_BUILD_ROOT/%{_sbindir}/rctvpvrd
ln -sf ../../etc/init.d/tvpvrd %{buildroot}/usr/sbin/rctvpvrd

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
# Remove the user that normally will run tvpvrd
# ---------------------------------------------------------------------------------
%postun
%insserv_cleanup
/usr/sbin/userdel tvpvrd 2> /dev/null ||
test -e /var/run/tvpvrd.pid  || rm -rf /var/run/tvpvrd.pid && : 

# ---------------------------------------------------------------------------------
# CHANGELOG (empty)
# ---------------------------------------------------------------------------------
%changelog

# ---------------------------------------------------------------------------------
# FILES
# ---------------------------------------------------------------------------------
%files
%defattr(-,root,root) 
/usr/bin/tvpvrd
/usr/sbin/rctvpvrd

%config /etc/tvpvrd
%config /etc/init.d/tvpvrd

%doc %attr(0444,root,root) /usr/share/man/man1/tvpvrd.1.gz
%doc COPYING AUTHORS README NEWS 
%doc docs/manpages/tvpvrd.1.pdf
%doc docs/manpages/tvpvrd.1.html
