#!/bin/sh
# 
# File:   switchchannel.sh
# Desc:   Template to do external channel switching.
# SVN:    $Id$
#
# Note: This is just a TEMPLATE you need to modify this so that it
# matches your LIRC setup.

#-----------------------------------------------------------
# Get options
#-----------------------------------------------------------
station=
while getopts s: o
do  case "$o" in
    s)      station="$OPTARG"
            ;;
    [?])    print >&2 "Usage; $0 -s station"
            exit 1
            ;;
    esac
done

if test -z station; then
    exit 1
fi

#-----------------------------------------------------------
# Depending on the station send the proper IR command
# NOte: The actual command must be specified in the
# lircd configuration file. In the following you need to
# replace "MYREMOTE" with the string corresponding to
# your defined device and "CHANNEL_?" must of course
# exist in your lircd configuration.
#-----------------------------------------------------------
case "$station" in
    tv1)    irsend SEND_START MYREMOTE CHANNEL_1
            sleep 1
            irsend SEND_STOP  MYREMOTE CHANNEL_1
            exit $?
            ;;

    tv2)    irsend SEND_START MYREMOTE CHANNEL_2
            sleep 1
            irsend SEND_STOP  MYREMOTE CHANNEL_2
            exit $?
            ;;

    kanal5) irsend SEND_START MYREMOTE CHANNEL_3
            sleep 1
            irsend SEND_STOP  MYREMOTE CHANNEL_3
            exit $?
            ;;

    *)      exit 1
            ;;
esac

exit 0





