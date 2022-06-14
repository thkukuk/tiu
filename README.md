# TIU - Transactional Image Update

This project aims to provide a robust update infrastructure for [opensuse
MicroOS](https://microos.opensuse.org) based on images and not packages (RPMs).

There are two key requirements for allowing robust updates of a system:

1. Redundancy: You must not update the system you are currently running on. Otherwise a failure during updating will brick the only system you can run your update from.
2. Atomicity: Writing the update to a currently inactive device is a critical operation. A failure occurring during this installation must not brick the device. The operation that switches the boot device must be atomic itself and only happen as the update was written error free.

Additional, no unauthorized entity should be able to update your device. There must be a secure channel to transfer the update and the update needs to be signed which allows to verify its author.

## Usage

### Installer image and update archives

There is a ready-to-use installer image with tiu and there are update archives
in the openSUSE Build Service.

The installer image is
[MicroOS-TIU-Installer.x86_64-livecd.iso](https://download.opensuse.org/repositories/home:/kukuk:/tiu/images/iso/MicroOS-TIU-Installer.x86_64-livecd.iso)

The archives can be found at
https://download.opensuse.org/repositories/home:/kukuk:/tiu/images/repo/swu/.
But it is normally not necessary to specify one, as the default archive is pre-configured.
The full project can be found at https://build.opensuse.org/project/monitor/home:kukuk:tiu

### Installation

* Boot the `MicroOS-TIU-Installer.x86_64-livecd.iso`
* Login as root (no password required)
* Start the installer: `tiu [--verbose|--debug] install -d /dev/<disk>`
  * `/dev/<disk>` is the device on which tiu will install the system. **All content of the disk will be erased!**
* Reboot
* During first boot, `ignition` and/or `combustion` will do the first initialization of the system. Documentation about how to create the input data can be found at https://en.opensuse.org/Portal:MicroOS/Ignition

### Update

From the running system call `tiu update`.

## TIU

`tiu` is a commandline interface to prepare a machine for a fresh installation
and to update partition based OS images using
[swupdate](https://swupdate.org/). The archives must be in the swu format.

### Features

* Different update sources
  * OTA (Network protocol support using libcurl (https, http, ftp, ssh, ...))
  * USB Stick

### Building TIU

#### Prerequires:
* meson
* openssl
* gio-2.0
* gio-unix-2.0
* libeconf
* libcurl
* swupdate

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

### Required Target Client Tools

The following tools are runtime requirements of  `tiu` to install
archives:
* swupdate

### Building tiu archives

`tiu` is using signed swu images.

### Extracting tiu archive

```
# mkdir -p /new/root
# tiu extract --archive system.swu --output /dev/*
```

## Generic Requirements for Updates

* Provide tags to identify, that the update image is compatible with this installation. This needs to be done with version numbers and hardware revesions from swupdate.
* Version number check, allow downgrades?
* Provide update image from one version to the next one, or all in one?
  * Since we work with images and provide full images, this doesn't matter, we can always update von any state to the current one as long as the real root including the `/etc` directory and layout stay compatible.
    * USB Sticks and container will have the full image in catar format, so we can update from every released version to the current one.
    * OTA: always download index files (inside swu archive), fetch only required blocks
* Image needs to be signed
* Must work with a plain http/https server, no server side services

## Filesystem Layout

### Generic Layout

#### /usrA and /usrB

TBD.

### `/etc`

systemd requires `/etc` writeable from the very beginning. This leaves the
following options:
* `/etc` is part of the read-only root filesystem and made writeable via overlayfs
  * plus: works like today with transactional-update
  * minus: does not allow IMA/EVM
* `/etc` is an own, writeable subvolume
  * plus: root filesystem is still read-only
  * minus: It seems it's not possible to mount `/etc` before systemd needs it writeable.
* We follow usrMove, which means we have a read-write root filesystem and the real data is only part of `/usr`.
  * plus: root filesystem is read-write and allows easier changes like creating additional directories
  * minus:
    * root filesysem is read-write and allows changes

### Layout outside `/usr`

The directories and files outside `/usr` are not part of the image and thus
needs to be created during installation.
The partition and filesystem layout is done by `setup-disk`, which uses
libstorage-ng to do the first setup. The initial symlinks for `/lib`, `/bin`
and similar are created by tiu.

## Requirements for distribution/RPMs

### RPM %pre/%post install scripts

The %pre/%post install scripts will only be executed when building the master image. Not when the image get's deployed or updated. So update code will never be executed. Changes to `/etc`, `/var` or anything else outside `/usr` will be deleted or overwritten.

### RPM filelist

RPMs should only contain files in `/usr`. Files outside this directory are not install- or updateable with the image and have to be generated or adjusted during the first boot of the new image with e.g. `tmpfiles.d`, `sysusers.d` or special systemd services.
%ghost entries for `/etc`, `/var`, `/run` and similar directories don't make any sense, as RPM is never executed to remove such packages with an update and the files will stay.

Distribution specific configuration files have to stay in `/usr`, e.g. `/usr/etc`, `/usr/share/<package>` or similar locations. `/etc` is only for host specific configuration files and admin made changes. See `systemd` or `[libeconf](https://github.com/openSUSE/libeconf)` as examples how services should merge the configuration snippets during start.

All files in `/etc` have to be generated or adjusted during first image installation by the `image installer` or during boot by systemd services, either unit files or `sysusers.d` and `tmpfiles.d` configuration files.
`tmpfiles.d` is also to be used to populate `/var` and `/run`.

### System accounts and file ownership

System accounts can only be created with systemd-sysusers (sysusers.d.5).
They will be created during the next boot after an update.

This means, that the image is not allowed to have files owned
by a new user. Best if all files are only owned by root.

Since files in `/var` or `/run` are not packaged in the image, but created
by systemd-tmpfiles (tmpfiles.d.5) during the next reboot, they can be
owned by a different system user than root.

Checks: `find . ! -user root` shouldn't find anything
