#!/bin/sh
# ------------------------------------------------------------------------------------------------------------
# Boostrap script for autotools. This script can be run after the sources have been
# cloned from the repo to create a working autotools setup.
#
# ------------------------------------------------------------------------------------------------------------

if [ ! -f configure ];
then

if [ -d /usr/share/gettext ]; then 
  ln -s /usr/share/gettext/config.rpath 
else 
  echo "ERROR: Cannot bootstrap! '/usr/share/gettext' is missing."
  echo "Please install GNU gettext first."
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

echo "ERROR: Cannot create autotools setup."
echo "       Try running \"autoreconf --install --symlink\" manually."

fi

else
echo "Ignored. Bootstrap is only supposed to be run once. File \"configure\" already exists."
echo "Try running ./stdbuild.sh to build the daemon."

fi
