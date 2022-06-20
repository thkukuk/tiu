# Install:
* SELinux support
* Make sure we don't install on the disk we are running from
* Use swupdate hardware support to find out if we install on the right disk
* Use mount.usr, fix ignition and combustion to work with this
* Create common-*-pc PAM configuration files

# Update:
* Check that the update images fits to the installed system and is newer
* Implement block based
  * Don't download full images for a few needed blocks
  * Add support for second URL pointing to castr repository
* Cleanup

# Building image:
* Add IMAGE_* tags to /etc/os-release

# General:
* Implement normal (means nearly sileng), verbose and debug mode/flags
* Move more of the script code into the main code

# Research
## EFI firmware/bootloader:
* have three partitions, which three entries in EFI firmware, usrA/usrB switch as default entry. All three should have independent boot loaders.
  * usrA
  * usrB
  * Rescue System
  
