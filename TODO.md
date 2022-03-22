# Install:

* Print full product name, version and architecture for fresh installations. Take data from Manifest.
* If no "--force" option is given, admin needs to explicit confirm that earsing the disk is ok.
* SELinux support
* Cleanup: after installation, remove snapper files in root of inst-sys

# Update:
* Check that the update images fits to the installed system and is newer
  * Use product name, version and architecture from manifest and compare with /usr/lib/os-release and arch

# Building image:
* Take the build number from the build environment and add it to the manifest

# General:
* Implement normal (means nearly sileng), verbose and debug mode/flags
* Move more of the script code into the main code

# Research
## EFI firmware/bootloader:
* have three partitions, which three entries in EFI firmware, usrA/usrB switch as default entry. All three should have independent boot loaders.
  * usrA
  * usrB
  * Rescue System
  
