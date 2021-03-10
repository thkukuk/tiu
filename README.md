# TIU - Transactional Image Update

There are two key requirements for allowing robust updates of a system:

1. Redundancy: You must not update the system you are currently running on. Otherwise a failure during updating will brick the only system you can run your update from.
2. Atomicity: Writing the update to a currently inactive device is a critical operation. A failure occurring during this installation must not brick the device. The operation that switches the boot device must be atomic itself and only happen as the update was written error free.

Additional, no unauthorized entity should be able to update your device. There must be a secure channel to transfer the update and the update needs to be signed which allows to verify its author.

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

* Verify that the format is really secure, how does the signing works?
