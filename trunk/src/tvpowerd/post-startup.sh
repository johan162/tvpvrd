#!/bin/sh
# File:   post-startup.sh
#
# Put any commands you want executed on the monitoring server just after the
# remote recording server has been started.
# A typical example would be to mount disks on the recording server. 
#
# Please recall that the daemon must be running as root in order to mount disks

# Example: The remote server exports a SMB share /data
#mount -t cifs -o username=smbuser,password=smbpassword //192.168.0.100/data /mnt/rec-data

