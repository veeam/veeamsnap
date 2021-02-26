# Kernel v5.9 and blk_interposer

For veeamsnap to work on kernels 5.9 and later it is required to add blk_interposer functionality.
Last patch for kernel was https://patchwork.kernel.org/project/linux-block/cover/1612881028-7878-1-git-send-email-sergei.shtepa@veeam.com/.
This patch can also be used for v5.10 and v5.11 kernels.

Therefore, only these two commits are required:
https://patchwork.kernel.org/project/linux-block/patch/1612881028-7878-3-git-send-email-sergei.shtepa@veeam.com/
https://patchwork.kernel.org/project/linux-block/patch/1612881028-7878-4-git-send-email-sergei.shtepa@veeam.com/

You can extract the patches from there, or you can use the patch from directory ./patch-v5_for_v5.11/.

In order to install a stable vanilla kernel (5.10 for example) you have to download it first:
```bash
wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.10.17.tar.xz
```
Unpack:
```bash
tar -xf ./linux-5.10.17.tar.xz
```
Apply the patch:
```bash
cd ./linux-5.10.17
patch -p1 <../v5-0002-block-add-blk_mq_is_queue_frozen.patch
patch -p1 <../v5-0003-block-add-blk_interposer.patch
```
Next, configure and build the kernel (as usual), and install it. blk_interposer does not require any additional configuration. I recommend to build an package (deb or rpm, depending on your packet manager):

```bash
make menuconfig; make -j4 deb-pkg
```
and install the kernel as a package.
