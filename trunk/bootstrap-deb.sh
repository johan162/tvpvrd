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
# ------------------------------------------------------------------------------------------------------------
# These packages are needed to be able to rebuild the documentation
# ------------------------------------------------------------------------------------------------------------
sudo apt-get -y -qq install docbook5-xml
sudo apt-get -y -qq install docbook5-xsl-ns # Note: We will manually upgrade to 1.78.1 but we install this to get the catalog paths
sudo apt-get -y -qq install libxml2-utils
sudo apt-get -y -qq install fop
sudo apt-get -y -qq install xsltproc

# Setup latest Docbook5 stylesheets manually the package are too old 
# (we use the namespace version)
mkdir -p /usr/share/xml/docbook/stylesheet/nwalsh5
cd /usr/share/xml/docbook/stylesheet/nwalsh5
wget -q https://sourceforge.net/projects/docbook/files/docbook-xsl-ns/1.78.1/docbook-xsl-ns-1.78.1.tar.bz2
tar xjf docbook-xsl-ns-1.78.1.tar.bz2
ln -s docbook-xsl-ns-1.78.1 current
rm docbook-xsl-ns-1.78.1.tar.bz2

# Make dbtoepub command executable to be able to run it
chmod +x /usr/share/xml/docbook/stylesheet/nwalsh5/current/epub/bin/dbtoepub

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
touch ChangeLog # This will be built when a release is made from the repository commits
automake --add-missing
autoreconf
else
echo "Ignored. Bootstrap is only supposed to be run once."
echo "Try running ./stdbuild.sh to build the daemon."
fi
