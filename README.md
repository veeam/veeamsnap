# veeamsnap
Veeam Agent for Linux kernel module

## Description
This kernel module implements snapshot and changed block tracking functionality used by Veeam Agent for Linux – simple and FREE backup agent designed to ensure the Availability of your Linux server instances, whether they reside in the public cloud or on premises.

## How to compile and install
Type `sudo make` command to compile the module.
Use `sudo make install` and `make uninstall` commands to install and uninstall the module.

## Create DKMS package for deb
```bash
sudo apt install debhelper
cd source
make VERSION_MAJOR=5 VERSION_MINOR=0 VERSION_REV=0 VERSION_BUILD=4318 dkms-deb-pkg
sudo apt install ../veeamsnap_5.0.0.4318_all.deb
```
Variables allow you to set any package version number. The package version must match the version of the veeam package you are using.

## Compatibility
This module supports Linux kernels from 2.6.32 to 5.8.

There are some problems with BFQ up to 5.2

Experimental supports for 5.14 kernel.

For kernels 5.9 and later [blk_interposer](./blk_interposer/README.md) is recommended, but not mandatory.

## How to use
For more information, please visit:
- How To blog post: https://www.veeam.com/blog/how-to-backup-linux.html
- Veeam community forums: https://forums.veeam.com/veeam-agent-for-linux-f41/

## Maintainer
Veeam Software Group GmbH veeam_team@veeam.com
