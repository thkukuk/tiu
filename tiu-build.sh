#!/bin/bash

if [ "$(id -u)" != "0" ]; then
	echo "ERROR: must be root to run the script!"
	exit 1
fi

build/tiu --debug create --tar https://download.opensuse.org/repositories/home:/kukuk:/tiu/images/openSUSE-MicroOS.x86_64-ContainerHost-tbz.tar.xz
if [ $? -ne 0 ]; then
    echo "ERROR downloading tar archive!"
    rm -rf "${WORKDIR}"
    exit 1
fi

# XXX create missing directories
#mkdir .snapshots
#chmod 750 .snapshots

#NON_ROOT_FILES=$(find . ! -user root)
#
#if [ -n "${NON_ROOT_FILES}" ]; then
#	echo "ERROR: there are files not owned by root"
#	echo "${NON_ROOT_FILES}"
#	popd > /dev/null
#	btrfs subvolume delete "${WORKDIR}/btrfs"
#	popd > /dev/null || exit
#        rm -rf "${WORKDIR}"
#	exit 1
#fi

