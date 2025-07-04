# veeamsnap
Veeam Agent for Linux kernel module

## Disclaimer
The project is deprecated.
The module will not support kernels 6.8 and later. It is replaced by the [blksnap](https://github.com/veeam/blksnap) project, the purpose of which is to offer it to the upstream.

## Description
This kernel module implements snapshot and changed block tracking functionality used by Veeam Agent for Linux – simple and FREE backup agent designed to ensure the Availability of your Linux server instances, whether they reside in the public cloud or on premises.

## How to compile and load
Type "make all" command to compile the module.
Use "make load" and "make unload" commands to load and unload the module.

## Compatibility
This module supports Linux kernels from 2.6.32 to 6.7.

There are some problems with BFQ up to 5.2

Also see [official page](https://www.veeam.com/kb2804).

## How to use
For more information, please visit:<br />
• How To blog post: https://www.veeam.com/blog/how-to-backup-linux.html<br />
• Veeam community forums: https://forums.veeam.com/veeam-agent-for-linux-f41/<br />

## Maintainer
Veeam Software Group GmbH veeam_team@veeam.com
