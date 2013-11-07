#!/bin/sh
#Upgrade script to install a new vesion of the tvpvrd daemon
#$Id$
sudo /etc/init.d/tvpvrd stop
sudo killall tvpvrd
sleep 3
sudo mv /tmp/tvpvrd.log /tmp/tvpvrd.log.OLD
make -j4
sudo make install
sudo /etc/init.d/tvpvrd start

