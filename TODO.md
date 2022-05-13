# Install:
* SELinux support
* Make sure we don't install on the disk we are running from
* Cleanup /var/lib/tiu/mount

# Update:
* Check that the update images fits to the installed system and is newer
  * Use product name, version and architecture from manifest and compare with /usr/lib/os-release and arch
* Use tiuidx archives by default
  * Don't download full images for a few needed blocks
  * Add support for second URL pointing to castr repository
* Cleanup
* Make new /usr snapshot default, even if not needed, to avoid people are able to delete the used /usr snapshot

# Building image:
* Take the build number from the build environment and add it to the manifest
* Add IMAGE_* tags to /etc/os-release

# General:
* Implement normal (means nearly sileng), verbose and debug mode/flags
* Move more of the script code into the main code
* Cleanup of old snapshots with corresponding kernel/initrd
* Sign TIU archive and verify signature before accessing it

# Research
## EFI firmware/bootloader:
* have three partitions, which three entries in EFI firmware, usrA/usrB switch as default entry. All three should have independent boot loaders.
  * usrA
  * usrB
  * Rescue System
  
