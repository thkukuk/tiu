# TIU - Transactional Image Update

This project aims to provide a robust update infrastructure for [opensuse
MicroOS](https://microos.opensuse.org) based on btrfs and images and not
packages (RPMs).

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

The TIU archives can be found at
https://download.opensuse.org/repositories/home:/kukuk:/tiu/images/repo/tiu/.
But it is normally not necessary to specify one, as the default TIU archive is pre-configured.
The full project can be found at https://build.opensuse.org/project/monitor/home:kukuk:tiu

### Installation

* Boot the `MicroOS-TIU-Installer.x86_64-livecd.iso`
* Login as root (no password required)
* Start the installer: `tiu install -d /dev/<disk>`
  * `/dev/<disk>` is the device on which tiu will install the system. **All content of the disk will be erased!**
* Reboot
* During first boot, `ignition` and/or `combustion` will do the first initialization of the system. Documentation about how to create the input data can be found at https://en.opensuse.org/Portal:MicroOS/Ignition

### Update

From the running system call `tiu update`.

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
# tiu create --tar system.tar.xz
```

`system.tar.xz` is a tar archive which includes the `/usr` directory of the system (`/usr/local` should be empty) and optional `/etc`. The content of the `/etc` directory will be moved to `/usr/share/factory/etc`.

### Extracting tiu archive

```
# mkdir -p /new/root
# tiu extract --archive system.tiutar --output /new/root
```

## Generic Requirements for Updates

* Provide tags to identify, that the update image is compatible with this installation. The details are described in [TIU-ARCHIVE](TIU-ARCHIVE.md)
* Version number check, allow downgrades?
* Provide update image from one version to the next one, or all in one?
  * Since we work with images and provide full images, this doesn't matter, we can always update von any state to the current one as long as the real root including the `/etc` directory and layout stay compatible.
  * Use [casync](https://github.com/systemd/casync):
    * USB Sticks and container will have the full image in catar format, so we can update from every released version to the current one.
    * OTA: always download caidx file (inside tiuidx archive), fetch only required blocks
* Image needs to be signed
* Must work with a plain http/https server, no server side services

## Filesystem Layout

### Generic Layout

#### btrfs

The following Partitions are necessary:
* `/` the root filesystem, btrfs
* `/boot/efi` for EFI firmware, vfat
* `/os` partition containing the data for `/usr`, btrfs

Optional, but highly recommended:
* `/var` to make sure, that especially containerized workload and `/var/cache` do not eat up all disk space of the root filesystem. No specific requirements for the filesystem.

For the first installation and for every update, a snapshot of `/` and `/os`
will be created. `/etc/fstab` in the new snapshot is adjusted to point to the
new snapshot of `/os`, so initial it points to
`/os/.snapshots/1/snapshot/usr`.

Due to bugs in casync the directory where the OS is stored needs to be a real
directory and no btrfs subvolume. Since we cannot mount directories directly,
we have to bind mount `/os/.snapshots/[NR]/snapshot/usr` to `/usr` in the
initrd.

For the first intallation, `/@/.snapshots/1/snapshot` is the default root
filesystem and `/os/.snapshots/1/snapshot/usr` will be bind mount to
`/usr`. After the first update, `/@/.snapshots/2/snapshot` will become the
default root filesystem and `/os/.snapshots/2/snapshot/usr` will be bind mount
to `/usr`.
While the snapshot numbers of `/` and `/os` will be most likely in sync, they
don't need so.

If a snapshot in `/@/.snapshots` get's removed during cleanup, we have to read
the `fstab` entry for `/usr` of that snapshot and delete the corresponding
snapshot in /os.

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

## casync

[casync](https://github.com/systemd/casync) seems to be the best tool to
syncronise a local system with a "remote" archive. It does not need a special
service to distribute the updates, a plain https server is enough. But there
are some bugs preventing the usage without workarounds:

* [Implicit seed of pre-existing output breaks with 'Stale file handle' error when reflinking #240](https://github.com/systemd/casync/issues/240)
* [Extracting in root of mounted btrfs filesystem fails with "Failed to run synchronizer: File exists" #248](https://github.com/systemd/casync/issues/248) - This one can be workarounded by package the tree as btrfs subvolume already.
