# TIU - Transactional Image Update

## Requirements

### System accounts and file ownership

System accounts can only be created with systemd-sysusers (sysusers.d.5).
They will be created during the next boot after an update.

This means, that the update-image is not allowed to have files owned
by a new user. Best is, if all files are owned by root.

Since files in /var or /run are not packaged in the image, but created
by systemd-tmpfiles (tmpfiles.d.5) during the next reboot, they can be
owned by a different system user than root.

Checks: `find . ! -user root` shouldn't find anything
