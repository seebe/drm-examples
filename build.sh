#! /bin/bash

POKY_TOOLCHAIN=/opt/poky/3.1.17

if [ "$CC" == "" ] ; then
	source ${POKY_TOOLCHAIN}/environment-setup-aarch64-poky-linux
fi


# Notes:
# * In the poky toolchain, the drm headers are under a /include/drm/ subdirectory, so if you build,
#   you will get a "fatal error: drm.h: No such file or directory " error because the sample code
#   assumes they are in /include/.
#   To fix this, we use the -I to add that directly manually to the include path
#
#  * We need to include the drm.so library as part of the build, so we add the command line -ldrm
#
#  * We need to include the libpng.so library as part of the build, so we add the command line -lpng

$CC -ldrm -lpng -I${POKY_TOOLCHAIN}/sysroots/aarch64-poky-linux/usr/include/drm plane-test.c -o plane-test


# Copy to TFTP direcotry so we can send over Ethernet. This makes it easy to
# try out a new build.
if [ -e /var/lib/tftpboot/ ] ; then
	cp -va plane-test /var/lib/tftpboot/
fi
# On the board, you would enter this command to transfer the file
#    tftp -g -r plane-test 10.10.10.30

