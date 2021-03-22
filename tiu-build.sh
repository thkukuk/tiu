#!/bin/bash

if [ "$(id -u)" != "0" ]; then
	echo "ERROR: must be root to run the script!"
	exit 1
fi

WORKDIR=$(mktemp -d /tmp/casync-workdir.XXXXXXXXXX)

pushd "${WORKDIR}" > /dev/null || exit

wget https://download.opensuse.org/repositories/home:/kukuk:/tiu/images/openSUSE-MicroOS.x86_64-ContainerHost-tbz.tar.xz
if [ $? -ne 0 ]; then
    echo "ERROR downloading tar archive!"
    popd > /dev/null || exit
    rm -rf "${WORKDIR}"
    exit 1
fi

btrfs subvolume create "btrfs"
if [ $? -ne 0 ]; then
    echo "ERROR creating btrfs subvolume"
    rm -rf "${WORKDIR}"
    popd > /dev/null || exit
    rm -rf "${WORKDIR}"
    exit 1
fi

cd btrfs

tar xf ../openSUSE-MicroOS.x86_64-ContainerHost-tbz.tar.xz
if [ $? -ne 0 ]; then
    echo "ERROR extracting tar archive!"
    popd > /dev/null || exit
    btrfs subvolume delete "${WORKDIR}/btrfs"
    rm -rf "${WORKDIR}"
    exit 1
fi


. usr/lib/os-release

# cleanup all mount point
rm -rfv etc/*
rm -rfv etc/.??*
rm -rfv home/*
rm -rfv root/*
rm -rfv root/.??*
rm -rfv opt/*
rm -rfv srv/*
rm -rfv boot/grub2/*-pc/*
rm -rfv boot/grub2/*-efi/*
rm -rfv boot/writable/*
rm -rfv usr/local/*
rm -rfv var/*
rm -rfv var/.??*
rm -rfv run/*
rm -rfv run/.??*

# create missing directories
mkdir .snapshots
chmod 750 .snapshots

NON_ROOT_FILES=$(find . ! -user root)

if [ -n "${NON_ROOT_FILES}" ]; then
	echo "ERROR: there are files not owned by root"
	echo "${NON_ROOT_FILES}"
	popd > /dev/null
	btrfs subvolume delete "${WORKDIR}/btrfs"
	popd > /dev/null || exit
        rm -rf "${WORKDIR}"
	exit 1
fi

popd > /dev/null || exit

CASYNC_ARGS="--exclude-submounts=yes --compression=zstd --with=best"
DIGEST=$(casync make ${CASYNC_ARGS} openSUSE-MicroOS-${VERSION_ID}.catar "${WORKDIR}/btrfs")
DIGEST2=$(casync make ${CASYNC_ARGS} --store openSUSE-MicroOS-${VERSION_ID}.castr openSUSE-MicroOS-${VERSION_ID}.caidx "${WORKDIR}/btrfs")

btrfs subvolume delete "${WORKDIR}/btrfs"

if [ "${DIGEST}" != "${DIGEST2}" ]; then
	echo "ERROR: data corruption!"
	echo "${DIGEST} != ${DIGEST2}"
	exit 1
fi

cat >> manifest.tiu << EOF
[global]
ID=${ID}
ARCH=x86_64
MIN_VERSION=20210101

[update]
VERSION=${VERSION_ID}
DIGEST=${DIGEST}

#[bundle]
#format=verity

#[image.rootfs]
#filename=openSUSE-MicroOS-${VERSION_ID}.tar.xz
EOF

mksquashfs manifest.tiu openSUSE-MicroOS-${VERSION_ID}.catar openSUSE-MicroOS-${VERSION_ID}.tiutar -comp xz -all-root -no-xattrs

mksquashfs manifest.tiu openSUSE-MicroOS-${VERSION_ID}.caidx openSUSE-MicroOS-${VERSION_ID}.tiuidx -comp xz -all-root -no-xattrs

mksquashfs manifest.tiu openSUSE-MicroOS-${VERSION_ID}.caidx openSUSE-MicroOS-${VERSION_ID}.castr openSUSE-MicroOS-${VERSION_ID}.tiustr -comp xz -all-root -no-xattrs
