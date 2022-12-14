#!/bin/sh
# ------------------------------------------------------------------------------------------------------------
# Boostrap script for Ubuntu/Debian/Linux Mint. Only needs to run once after
# the respoitory has been downloaded from the server and not the installation
# file.
#
# This script will first make sure that all needed develipment packages are
# installed and then run the bootstrap for the autotools 
# ------------------------------------------------------------------------------------------------------------

if [ ! -f configure ];
then

WORKING_DIR=`pwd`

sudo apt-get -y -qq install autoconf
sudo apt-get -y -qq install libtool

# ------------------------------------------------------------------------------------------------------------
# These packages are needed to be able to rebuild the documentation
# ------------------------------------------------------------------------------------------------------------
sudo apt-get -y -qq install docbook5-xml
sudo apt-get -y -qq install docbook-xsl-ns 
sudo apt-get -y -qq install libxml2-utils
sudo apt-get -y -qq install fop
sudo apt-get -y -qq install xsltproc

# Setup latest Docbook5 stylesheets manually the package are too old 
# (we use the namespace version)
sudo mkdir -p /usr/share/xml/docbook/stylesheet/nwalsh5
cd /usr/share/xml/docbook/stylesheet/nwalsh5
if [ ! -d docbook-xsl-ns-1.78.1 ]; then
  sudo wget -q https://sourceforge.net/projects/docbook/files/docbook-xsl-ns/1.78.1/docbook-xsl-ns-1.78.1.tar.bz2
  sudo tar xjf docbook-xsl-ns-1.78.1.tar.bz2
  sudo rm docbook-xsl-ns-1.78.1.tar.bz2
fi
sudo ln -sf docbook-xsl-ns-1.78.1 current
cd $WORKING_DIR 

# Make dbtoepub command executable to be able to run it
sudo chmod +x /usr/share/xml/docbook/stylesheet/nwalsh5/current/epub/bin/dbtoepub

# ------------------------------------------------------------------------------------------------------------
# These packages are needed for the core build
# ------------------------------------------------------------------------------------------------------------
sudo apt-get -y -qq install libpcre3-dev
sudo apt-get -y -qq install libxml2-dev
sudo apt-get -y -qq install libreadline-dev
sudo apt-get -y -qq install automake

# ------------------------------------------------------------------------------------------------------------
# Needed for transcoding
# ------------------------------------------------------------------------------------------------------------
sudo apt-get -y -qq install ffmpeg

# ------------------------------------------------------------------------------------------------------------
# Finally we can do the initial bootstrap to construct the initial configure
# ------------------------------------------------------------------------------------------------------------
if [ -d /usr/share/gettext ]; then 
  ln -s /usr/share/gettext/config.rpath 
else 
  echo "ERROR: Cannot bootstrap! '/usr/share/gettext' is missing"
  exit 1
fi
touch ChangeLog 
autoreconf --install --symlink

if [ "$?" = 0 ]; then 

echo "--------------------------------------------------------------"
echo " DONE. Build environment is ready. "
echo " "
echo " You can now run \"./stdbuild.sh\" to build the daemon "
echo " and then then run \"./mkrelease.sh\" to create new releases. "
echo "--------------------------------------------------------------"

else

echo "ERROR: Cannot setup build environment. Sorry."
echo "       Try running \"autoreconf --install --symlink\" manually."

fi

else
echo "Ignored. Bootstrap is only supposed to be run once."
echo "Try running ./stdbuild.sh to build the daemon."
fi

