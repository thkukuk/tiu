# TIU - Transactional Image Update

There are two key requirements for allowing robust updates of a system:

1. Redundancy: You must not update the system you are currently running on. Otherwise a failure during updating will brick the only system you can run your update from.
2. Atomicity: Writing the update to a currently inactive device is a critical operation. A failure occurring during this installation must not brick the device. The operation that switches the boot device must be atomic itself and only happen as the update was written error free.

Additional, no unauthorized entity should be able to update your device. There must be a secure channel to transfer the update and the update needs to be signed which allows to verify its author.

## Requirements for update

* Provide tags to identify, that the update image is compatible with this installation
* Version number check, allow downgrades?
* Provide update image from one version to the next one, or all in one?
  * One version to next one is much smaller as it only contains the differences, but you have to apply all updates in the correct order to come from an old version to the current one
  * With all updates (or better the full installation) in one image, you can update from every version to the current one. But how to update efficient without transferring too much data?
  * SOLUTION: use casync.
    * USB Sticks and container will have the full image in catar format, so we can update from every released version to the current one.
    * OTA: always download caidx file (inside tiuidx archive), fetch only required blocks 
* Image needs to be signed

## Requirements for distribution/RPMs

### System accounts and file ownership

System accounts can only be created with systemd-sysusers (sysusers.d.5).
They will be created during the next boot after an update.

This means, that the update-image is not allowed to have files owned
by a new user. Best is, if all files are owned by root.

Since files in /var or /run are not packaged in the image, but created
by systemd-tmpfiles (tmpfiles.d.5) during the next reboot, they can be
owned by a different system user than root.

Checks: `find . ! -user root` shouldn't find anything

## TODO

- [ ] Build image as tar archive from MicroOS RPMs in OBS
- [ ] Create manifest which defines:
  - [ ] ID of OS which is updateable (/etc/os-release)
  - [ ] Supported architecture
  - [ ] Minimum version of OS which can be updated
  - [ ] Version of update
  - [ ] Digest of image (casync digest)
- [ ] Verify that the format is really secure
- [ ] Use dm-verity (see RAUC)
- [ ] Build tiutar archive (squashfs image with manifest and catar archive)
- [ ] Build tiuidx archive (swuashfs image with manifest and caidx file, store available via http/https)
- [ ] SELinux (installs in /etc and /var)
- [ ] update-alternatives (creates data in /var and symlinks in /etc)
