# Building image:
* Add IMAGE_* tags to /etc/os-release
* Make sure the filesystem image is as small as possible
* Add dm-verity checksums to the image
* Set SELinux labels
* Better OBS support in regard to signing the image

# Install:
* SELinux support
* Make sure we don't install on the disk we are running from
* Robust way to not pre/post scripts from swu image during installation

# Update:
* Put swupdate/tiu into a container, so that we can update the update stack
* How to tell swupdate via IPC to download the image itself?
* Check that the update images fits to the installed system and is newer
* Implement block based (Delta) update (swupdate)
  * Don't download full images for a few needed blocks
* How to update the bootloader?
* How to handle /etc for rollback?
* How to handle/update /boot/grub2/grub.cfg?
* Setup chroot to create initrd with dracut

# General:
* For every USR_X partition have a ROOT_X partition
  * How to handle /root, /usr/local across all ROOT_X partitions? Rollback?
  * How to sync ROOT_X with ROOT_(X-1) at first boot of ROOT_X?
* How to handle /etc? Rollback of config, too
* Improve logging: normal (means nearly silent), verbose and debug mode
* Move more of the script code into the main code
* Rescue partition/system
* Factory reset
* Configuration file reset (keep things like machine-id)
