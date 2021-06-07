# veeamsnap
Veeam Agent for Linux kernel module

## Description
This kernel module implements snapshot and changed block tracking functionality used by Veeam Agent for Linux – simple and FREE backup agent designed to ensure the Availability of your Linux server instances, whether they reside in the public cloud or on premises.

## How to compile and install
Type "sudo make" command to compile the module.
Use "sudo make install" and "make uninstall" commands to install and uninstall the module.

## Create DKMS package for deb
"sudo apt install debhelper"
"make VERSION_MAJOR=5 VERSION_MINOR=0 VERSION_REV=0 VERSION_BUILD=4318 dkms-deb-pkg"
Variables allow you to set any package version number. The package version must match the version of the veeam package you are using.

## Compatibility
This module supports Linux kernels from 2.6.32 to 5.8.

There are some problems with BFQ up to 5.2

Experimental supports kernels from 5.8 to 5.12.

For kernels 5.9 and later [blk_interposer](./blk_interposer/README.md) is recommended, but not mandatory.

## How to use
For more information, please visit:<br />
• How To blog post: https://www.veeam.com/blog/how-to-backup-linux.html<br />
• Veeam community forums: https://forums.veeam.com/veeam-agent-for-linux-f41/<br />

## Maintainer
Veeam Software Group GmbH veeam_team@veeam.com
