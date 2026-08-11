# Orange Pi 4 Pro dual-boot script (stock default + optional can1).
# Select via /boot/orangepiEnv.txt:
#   kernel_flavor=stock   # default; loads uImage + uInitrd-6.6.98-sun60iw2
#   kernel_flavor=can1    # loads uImage-can1 + uInitrd-6.6.98-sun60iw2-can1
# Never overwrite the stock uImage filename for experiments.
#
# Compile on board:
#   mkimage -C none -A arm -T script -d boot-sun60iw2-dual.cmd /boot/boot.scr

setenv load_addr "0x43100000"
setenv overlay_error "false"
setenv verbosity "1"
setenv console "both"
setenv bootlogo "false"
setenv rootfstype "ext4"
setenv docker_optimizations "on"
setenv earlycon "on"
setenv kernel_flavor "stock"

echo "Boot script loaded from ${devtype} ${devnum}"

if test -e ${devtype} ${devnum} ${prefix}orangepiEnv.txt; then
	load ${devtype} ${devnum} ${load_addr} ${prefix}orangepiEnv.txt
	env import -t ${load_addr} ${filesize}
fi

if test "${logo}" = "disabled"; then setenv logo "logo.nologo"; fi

if test "${console}" = "display" || test "${console}" = "both"; then setenv consoleargs "console=tty1"; fi
if test "${console}" = "serial" || test "${console}" = "both"; then setenv consoleargs "console=ttyS0,115200 ${consoleargs}"; fi
if test "${earlycon}" = "on"; then setenv consoleargs "earlyprintk=sunxi-uart,0x02500000 initcall_debug=0 ${consoleargs}"; fi
if test "${bootlogo}" = "true"; then
	setenv consoleargs "splash plymouth.ignore-serial-consoles ${consoleargs}";
else
	setenv consoleargs "splash=verbose ${consoleargs}"
fi

part uuid ${devtype} ${devnum}:1 partuuid

if test -z "${rootdev}"; then rootdev=PARTUUID="${partuuid}"; fi

# Dual-boot file selection. Stock remains the safe default.
if test "${kernel_flavor}" = "can1"; then
	setenv kernel_image "uImage-can1"
	setenv initrd_image "uInitrd-6.6.98-sun60iw2-can1"
	echo "Selected kernel_flavor=can1"
else
	setenv kernel_image "uImage"
	setenv initrd_image "uInitrd-6.6.98-sun60iw2"
	echo "Selected kernel_flavor=stock"
fi

setenv bootargs "root=${rootdev} rootwait rootfstype=${rootfstype} ${consoleargs} consoleblank=0 loglevel=${verbosity} clk_ignore_unused swiotlb=65536 usb-storage.quirks=${usbstoragequirks} ${extraargs} ${extraboardargs}"

if test "${docker_optimizations}" = "on"; then setenv bootargs "${bootargs} cgroup_enable=cpuset cgroup_memory=1 cgroup_enable=memory swapaccount=1"; fi

load ${devtype} ${devnum} ${ramdisk_addr_r} ${prefix}${initrd_image}
load ${devtype} ${devnum} ${kernel_addr_r} ${prefix}${kernel_image}

load ${devtype} ${devnum} ${fdt_addr_r} ${prefix}dtb/${fdtfile}
fdt addr ${fdt_addr_r}
fdt resize 65536
for overlay_file in ${overlays}; do
	if load ${devtype} ${devnum} ${load_addr} ${prefix}dtb/allwinner/overlay/${overlay_prefix}-${overlay_file}.dtbo; then
		echo "Applying kernel provided DT overlay ${overlay_prefix}-${overlay_file}.dtbo"
		fdt apply ${load_addr} || setenv overlay_error "true"
	fi
done
for overlay_file in ${user_overlays}; do
	if load ${devtype} ${devnum} ${load_addr} ${prefix}overlay-user/${overlay_file}.dtbo; then
		echo "Applying user provided DT overlay ${overlay_file}.dtbo"
		fdt apply ${load_addr} || setenv overlay_error "true"
	fi
done

bootm ${kernel_addr_r} ${ramdisk_addr_r} ${fdt_addr_r}
