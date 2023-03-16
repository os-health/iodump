KERNELVER?=$(shell uname -r)

default:
	make -C user
	make -C kernel
clean:
	make -C user clean
	make -C kernel clean
install:
	if [ $(id -u) -ne 0 ]; then { echo "root permission is required to install iodump";exit 0;} fi
	install -d                           /usr/lib/iodump/
	install kernel/kiodump.ko            /usr/lib/iodump/kiodump.ko
	install -d                           /usr/src/os_health/iodump/
	install kernel/kiodump.conf          /usr/src/os_health/iodump/kiodump.conf
	install user/iodump                  /usr/sbin/iodump
	ps h -C iodump -o pid && killall iodump || true
	if [ -e /sys/module/kiodump/parameters/enable ];then { echo N > /sys/module/kiodump/parameters/enable;} fi
	lsmod | grep -qw kiodump && modprobe -r kiodump || true
	if [ -e "/usr/lib/iodump/kiodump.ko" ];then \
	    install -d /lib/modules/$(KERNELVER)/extra/os_health/iodump/;\
	    install /usr/lib/iodump/kiodump.ko /lib/modules/$(KERNELVER)/extra/os_health/iodump/kiodump.ko;\
	    /sbin/depmod -aeF /boot/System.map-$(KERNELVER) $(KERNELVER) > /dev/null;\
	    modprobe kiodump;\
	    if [ $$? -ne 0 ];then { echo "kiodump.ko modprobe is failed, check the dmesg info.";exit 0;} fi;\
	else\
	    echo "The /usr/lib/iodump/kiodump.ko file does not exist in the iodump rpm package";\
	    exit 0; \
	fi
	if [ -d /etc/modules-load.d/ ];then { install /usr/src/os_health/iodump/kiodump.conf /etc/modules-load.d/kiodump.conf;} fi
uninstall:
	if [ $(id -u) -ne 0 ]; then { echo "root permission is required to uninstall iodump";exit 0;} fi
	ps h -C iodump -o pid && killall iodump || true
	if [ -e /sys/module/kiodump/parameters/enable ];then { echo N > /sys/module/kiodump/parameters/enable;} fi
	lsmod | grep -qw kiodump && modprobe -r kiodump || true
	rm -f /lib/modules/$(KERNELVER)/extra/os_health/iodump/kiodump.ko
	/sbin/depmod -aeF /boot/System.map-$(KERNELVER) $(KERNELVER) > /dev/null
	rm -f /etc/modules-load.d/kiodump.conf
	rm -fr /usr/lib/iodump/
	rm -fr /usr/src/os_health/iodump/
	rm -f  /usr/sbin/iodump
