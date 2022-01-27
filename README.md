# TIU - Transactional Image Update

This project aims to provide a robust update infrastructure for [opensuse MicroOS](https://microos.opensuse.org) based on btrfs and images and not packages (RPMs).

There are two key requirements for allowing robust updates of a system:

1. Redundancy: You must not update the system you are currently running on. Otherwise a failure during updating will brick the only system you can run your update from.
2. Atomicity: Writing the update to a currently inactive device is a critical operation. A failure occurring during this installation must not brick the device. The operation that switches the boot device must be atomic itself and only happen as the update was written error free.

Additional, no unauthorized entity should be able to update your device. There must be a secure channel to transfer the update and the update needs to be signed which allows to verify its author.

## TIU

`tiu` is heavily inspired by [Rauc](https://github.com/rauc/rauc/). It
controls the update process on systems using atomic updates and is both, a
build host tool that allows to create TIU archives and an update client.

### Features

* Different update sources
  * OTA (Network protocol support using libcurl (https, http, ftp, ssh, ...))
  * USB Stick
* Network streaming mode using casync
  * chunk-based binary delta updates
  * significantly reduce download size
  * no extra storage required


### Building TIU

#### Prerequires:
* meson
* openssl
* gio-2.0
* gio-unix-2.0
* libeconf
* libcurl

#### Build:
```
# meson <builddir>
# cd <builddir>
# meson compile
```

#### Installation:
```
# meson install
```

### Required Host Tools

The following tools are runtime requirements of `tiu` to build tiu archives:
* tar
* casync
* btrfs utility
* mksquashfs

### Required Target Client Tools

The following tools are runtime requirements of  `tiu` to install tiu
archives:
* casync

### Building tiu archives

```
# tiu create --tar system-update.tar.xz
```

This will try to create a `btrfs` subvolume in _$TMPDIR_.  If this
directory is not under a `btrfs` filesystem `tiu` will complain with
"ERROR: not a btrfs filesystem: /tmp/tiu-workdir-XXXXXXXXXX" message.
The solution is execute it with a different default directory.

```
# TMPDIR=/var/cache/tiu tiu create --tar system-update.tar.xz
```

### Extracting tiu archive

```
# mkdir -p /new/root
# tiu extract --archive system-update.tiutar --output /new/root
```

## Generic Requirements for Updates

* Provide tags to identify, that the update image is compatible with this installation
* Version number check, allow downgrades?
* Provide update image from one version to the next one, or all in one?
  * One version to next one is much smaller as it only contains the differences, but you have to apply all updates in the correct order to come from an old version to the current one
  * With all updates (or better the full installation) in one image, you can update from every version to the current one. But how to update efficient without transferring too much data?
  * SOLUTION: use casync.
    * USB Sticks and container will have the full image in catar format, so we can update from every released version to the current one.
    * OTA: always download caidx file (inside tiuidx archive), fetch only required blocks
* Image needs to be signed
* Must work with a plain http/https server, no server side services

## Filesystem Layout

`systemd` requires `/etc` writeable from the very beginning. This leaves the
following options:
* `/etc` is part of the read-only root filesystem and made writeable via overlayfs
  * plus: works like today with transactional-update
  * minus: does not allow IMA/EVM
* `/etc` is an own, writeable subvolume
  * plus: root filesystem is still read-only
  * minus: I was not able to mount `/etc` before systemd needs it writeable.
* We follow usrMove, which means we have a read-write root filesystem and the real data is only part of `/boot` and `/usr`.
  * plus: root filesystem is read-write and allows easier changes like creating additional directories
  * minus:
    * root filesysem is read-write and allows changes
    * rollback of two independent volumes is not possible

## Requirements for distribution/RPMs

### RPM %pre/%post install scripts

The %pre/%post install scripts will only be executed when building the master image. Not when the image get's deployed or updated. So update code will never be executed. Changes to /etc, /var or anything else outside /usr and /boot will be deleted or overwritten.

### RPM filelist

RPMs should only contain files in `/usr` and for the moment in `/boot`. Files outside this two directories are not updateable and will always stay at the version of the image with which the System was installed first. %ghost entries for `/etc`, `/var`, `/run` and similar directories don't make any sense, as RPM is never executed to remove such packages with an update and the files will stay.

Distribution specific configuration files have to stay in `/usr`, e.g. `/usr/etc`, `/usr/share/<package>` or similar locations. `/etc` is only for host specific configuration files and admin made changes. See `systemd`, `dbus-1` or `libeconf` as examples how services should merge the configuration snippets during start.

All files in `/etc` have to be generated or adjusted during first image installation by the `image installer` or during boot by systemd services, either unit files or `sysusers.d` and `tmpfiles.d` configuration files.
`tmpfiles.d` is also to be used to populate `/var` and `/run`.

### System accounts and file ownership

System accounts can only be created with systemd-sysusers (sysusers.d.5).
They will be created during the next boot after an update.

This means, that the update-image is not allowed to have files owned
by a new user. Best is, if all files are owned by root.

Since files in /var or /run are not packaged in the image, but created
by systemd-tmpfiles (tmpfiles.d.5) during the next reboot, they can be
owned by a different system user than root.

Checks: `find . ! -user root` shouldn't find anything

## casync

[casync](https://github.com/systemd/casync) seems to be the best tool to
syncronise a local system with a "remote" archive. It does not need a special
service to distribute the updates, a plain https server is enough. But there
are some bugs preventing the usage without workarounds:

* [Implicit seed of pre-existing output breaks with 'Stale file handle' error when reflinking #240](https://github.com/systemd/casync/issues/240)
* [Extracting in root of mounted btrfs filesystem fails with "Failed to run synchronizer: File exists" #248](https://github.com/systemd/casync/issues/248) - This one can be workarounded by package the tree as btrfs subvolume already.


## TODO

- [x] Build image as tar archive from MicroOS RPMs in OBS
- [x] Create manifest which defines:
  - [x] ID of OS which is updateable (/etc/os-release)
  - [x] Supported architecture
  - [x] Minimum version of OS which can be updated
  - [x] Version of update
  - [ ] Digest of image (casync digest)
- [ ] Verify that the format is really secure
- [x] Use dm-verity (see RAUC)
- [x] Build tiutar archive (squashfs image with manifest and catar archive)
- [x] Build tiuidx archive (swuashfs image with manifest and caidx file, store available via http/https)
- [ ] SELinux (installs in /etc and /var)
- [x] Kernel update incl. rebuilding initrd
- [x] Bootloader update
