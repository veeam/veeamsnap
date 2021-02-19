# Kernel v5.9 and blk_interposer

For veeamsnap to work on kernels 5.9 and later it is required to add blk_interposer functionality.
This feature is being developed in git://git.kernel.org/pub/scm/linux/kernel/git/hare/scsi-devel blk-interposer.v2 branch. One needs only kernel changes (skip device-mapper). This patch can also be used for v5.11 kernel.

Therefore, only these two commits are required:
```
commit 8dddc3dd22db58c690ee2916266b136a1b80875b
Author: Sergei Shtepa <sergei.shtepa@veeam.com>
Date:   Thu Nov 19 17:49:23 2020 +0100
blk_interposer - Block Layer Interposer

commit 71e21b5e3941f4c0cacad47cc119141ca7fd2a66
Author: Sergei Shtepa <sergei.shtepa@veeam.com>
Date:   Fri Dec 11 19:01:19 2020 +0300
block: blk_interposer - change attach/detach logic
```
If are not into having too much fun with git, you can use the prepared patches from this branch:
```
 0001-blk_interposer-Block-Layer-Interposer.patch
 0003-block-blk_interposer-change-attach-detach-logic.patch
```
or use this cumulative patch:
```
blk_interposer_cumulative.patch
```
In order to install a stable vanilla kernel (5.10 for example) you have to download it first:
```bash
wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.10.7.tar.xz
```
Unpack:
```bash
tar -xf ./linux-5.10.7.tar.xz
```
Apply the patch:
```bash
cd ./linux-5.10.7
patch -p1 <../0001-blk_interposer-Block-Layer-Interposer.patch
patch -p1 <../0003-block-blk_interposer-change-attach-detach-logic.patch
```
Or apply the cumulative patch:
```bash
patch -p1 <../blk_interposer_cumulative.patch
```
Next, configure and build the kernel (as usual), and install it. blk_interposer does not require any additional configuration. I recommend to build an package (deb or rpm, depending on your packet manager):

```bash
make menuconfig; make -j4 deb-pkg
```
and install the kernel as a package.
