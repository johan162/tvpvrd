#!/bin/sh
#Upgrade script to install a new vesion of the tvpvrd daemon
#$Id$
sudo /etc/init.d/tvpvrd stop
sleep 5
sudo make install
sudo /etc/init.d/tvpvrd start

