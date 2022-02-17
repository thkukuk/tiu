# Format of TIU Archives

## Formats

There are two TIU archive formats:
* *.tiutar - compressed squashfs image with caidx index
* *.tiuidx - compressed squashfs image with catar archive


## Design

A TIU archive consists of:
* Squashfs image, protected by dm-verity, containing:
  * manifest.tiu
  * casync catar or caidx archive
* Header in ini-style format
  * format
  * dm-verity salt
  * dm-verity hash
  * dm-verity size
* Pointer to the heads

## manifest.tiu

The manifest.tiu file has several categories:

### global

The global section contains informations to identify the OS version inside the
archive and additional informations to find out if it is compatible with the
installed OS:

* ID - this is the `ID` from the `/usr/lib/os-release` file.
* ARCH - the architecture of the system.
* FULL_NAME - the `PRETTY_NAME` of `/usr/lib/os-release`.
* PRODUCT_NAME - the `NAME` of `/usr/lib/os-release`, where all spaces are replaced with '-'.
* VERSION - the `VERSION` from `/usr/lib/os-release`.

### update

This section contains all relevant informations to define, which system can
how updated.

* MIN_VERSION - the version number of the installed OS, which is at least required.
* FORMAT - `caidx` or `catar`.
* ARCHIVE - Name of the caidx or catar file.
