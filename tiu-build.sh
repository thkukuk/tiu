#!/bin/bash

if [ "$(id -u)" != "0" ]; then
	echo "ERROR: must be root to run the script!"
	exit 1
fi

rm -f openSUSE-MicroOS.x86_64-ContainerHost-tbz.tar.xz
wget https://download.opensuse.org/repositories/home:/kukuk:/tiu/images/openSUSE-MicroOS.x86_64-ContainerHost-tbz.tar.xz

rm -rf workdir
mkdir workdir
pushd workdir > /dev/null || exit
tar xf ../openSUSE-MicroOS.x86_64-ContainerHost-tbz.tar.xz

. etc/os-release

# cleanup all mount point
rm -rfv home/*
rm -rfv root/*
rm -rfv opt/*
rm -rfv srv/*
rm -rfv boot/grub2/*-pc/*
rm -rfv boot/grub2/*-efi/*
rm -rfv boot/writeable/*
rm -rfv usr/local/*
rm -rfv var/*
rm -rfv run/*

NON_ROOT_FILES=$(find . ! -user root)

if [ -n "${NON_ROOT_FILES}" ]; then
	echo "ERROR: there are files not owned by root"
	echo "${NON_ROOT_FILES}"
	popd > /dev/null
	exit 1
fi

#tar -cJf ../openSUSE-MicroOS-${VERSION_ID}.tar.xz *

popd > /dev/null || exit

CASYNC_ARGS="--exclude-submounts=yes --compression=zstd --with=best"
DIGEST=$(casync make ${CASYNC_ARGS} openSUSE-MicroOS-${VERSION_ID}.catar workdir)
DIGEST2=$(casync make ${CASYNC_ARGS} --store openSUSE-MicroOS-${VERSION_ID}.castr openSUSE-MicroOS-${VERSION_ID}.caidx  workdir/)

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
