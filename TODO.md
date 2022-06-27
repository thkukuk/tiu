# Bootloader:
* Create automatically entries for USR_A/USR_B/... partitions

# Install:
* SELinux support
* Make sure we don't install on the disk we are running from
* Use swupdate hardware support to find out if we install on the right disk
* Use mount.usr, fix ignition and combustion to work with this
* Create common-*-pc PAM configuration files

# Update:
* Check that the update images fits to the installed system and is newer
* Implement block based update
  * Don't download full images for a few needed blocks
* Cleanup

# Building image:
* Add IMAGE_* tags to /etc/os-release
* Make sure the filesystem image is as small as possible

# General:
* Implement normal (means nearly silent), verbose and debug mode/flags
* Move more of the script code into the main code
* How to handle /etc? Rollback of config, too
* Rescue partition/system
* Factory reset
* Configuration file reset (keep things like machine-id)
