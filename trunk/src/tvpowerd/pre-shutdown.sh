#!/bin/sh
# File:   pre-shutdown.sh
#
# Put any commands you want executed on the monitoring server before you shutdown the
# recording server.
# A typical example would be to unmount disks on the recording server. Having existing
# mounts, for example NFS mounts, is bad. This can cause all kinds of lock-ups and
# freezes so it is relly important to unmount any such shares before the remote
# server is shut down
#
# Please recall that the daemon must be running as root in order to unmopunt the
# disks.

#umount -f -l /data/mn/recordingdata
 