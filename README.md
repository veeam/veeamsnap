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
make VERSION_MAJOR=5 VERSION_MINOR=0 VERSION_REV=2 VERSION_BUILD=4567 dkms-deb-pkg
sudo apt install ../veeamsnap_5.0.2.4567_all.deb
```
Variables allow you to set any package version number. The package version must match the version of the veeam package you are using.

## Compatibility
This module supports Linux kernels from 2.6.32 to 5.16-rc6.

There are some problems with BFQ up to 5.2

## How to use
For more information, please visit:
- How To blog post: https://www.veeam.com/blog/how-to-backup-linux.html
- Veeam community forums: https://forums.veeam.com/veeam-agent-for-linux-f41/

## Maintainer
Veeam Software Group GmbH veeam_team@veeam.com
