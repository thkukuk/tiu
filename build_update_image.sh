#!/bin/bash

if [ "$(id -u)" != "0" ]; then
	echo "ERROR: must be root to run the script!"
	exit 1
fi

rm -f openSUSE-MicroOS.x86_64-ContainerHost-tbz.tar.xz
wget https://download.opensuse.org/repositories/home:/kukuk:/tiu/images/openSUSE-MicroOS.x86_64-ContainerHost-tbz.tar.xz

rm -rf workdir
mkdir workdir
pushd workdir || exit
tar xf ../openSUSE-MicroOS.x86_64-ContainerHost-tbz.tar.xz

find . ! -user root

rm -rf *

mv ../openSUSE-MicroOS.x86_64-ContainerHost-tbz.tar.xz .

popd || exit

openssl req -x509 -newkey rsa:4096 -nodes -keyout demo.key.pem -out demo.cert.pem -subj "/O=rauc Inc./CN=rauc-demo"

cat >> workdir/manifest.raucm << EOF
[update]
compatible=rauc-demo-x86
version=2015.04-1

[bundle]
format=verity

[image.rootfs]
filename=openSUSE-MicroOS.x86_64-ContainerHost-tbz.tar.xz
EOF

rauc --cert demo.cert.pem --key demo.key.pem bundle workdir/ update-2015.04-1.raucb
