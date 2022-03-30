# Install:

* SELinux support
* Cleanup: after installation, remove snapper files in root of inst-sys
* setup-disk: returns 0 even if old disklayout cannot be wiped
* Support tiuidx archives (requires second url pointing to castr repository)

# Update:
* Check that the update images fits to the installed system and is newer
  * Use product name, version and architecture from manifest and compare with /usr/lib/os-release and arch
* Use tiuidx archives by default
  * Don't download full images for a few needed blocks
  * Add support for second URL pointing to castr repository
* Cleanup

# Building image:
* Take the build number from the build environment and add it to the manifest

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
  
