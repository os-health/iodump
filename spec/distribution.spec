%global __os_install_post %{nil}
%define _ignore_post_scripts_errors %{nil}
%define _enable_debug_packages %{nil}
%define debug_package  %{nil}
%define all_kver       %(ls /usr/src/kernels/ | cat)
%define anolis_release 1

Name:                  iodump
Version:               1.0.1
Release:               %{anolis_release}%{?dist}
Url:                   https://gitee.com/anolis/iodump.git
Summary:               io details
Group:                 System Environment/Base
License:               MIT
Source0:               %{name}-%{version}.tar.gz

Vendor:                Alibaba

%description
dump the io details 

%prep
cd $RPM_BUILD_DIR
rm -rf %{name}-%{version}
gzip -dc $RPM_SOURCE_DIR/%{name}-%{version}.tar.gz | tar -xvvf -
if [ $? -ne 0 ]; then
    exit $?
fi
main_dir=$(tar -tzvf $RPM_SOURCE_DIR/%{name}-%{version}.tar.gz| head -n 1 | awk '{print $NF}' | awk -F/ '{print $1}')
if [ "${main_dir}" == %{name} ];then
    mv %{name} %{name}-%{version}
fi
cd %{name}-%{version}
chmod -R a+rX,u+w,g-w,o-w .
 
%build
cd %{name}-%{version}
[ ! -e kiodump ] && install -d kiodump

cd ./kernel/
for k in $(ls /usr/src/kernels/)
do
    make KVER=${k} clean
    make KVER=${k}
    install kiodump.ko ./../kiodump/kiodump-${k}.ko
    make KVER=${k} clean
done

cd ./../user/
make

%install
BuildDir=$RPM_BUILD_DIR/%{name}-%{version}
install -d                             %{buildroot}/usr/lib/iodump/
install $BuildDir/kiodump/*            %{buildroot}/usr/lib/iodump/
install -d                             %{buildroot}/usr/src/os_health/iodump/
install $BuildDir/kernel/kiodump.conf  %{buildroot}/usr/src/os_health/iodump/kiodump.conf
install -d                             %{buildroot}/usr/sbin/
install $BuildDir/user/iodump          %{buildroot}/usr/sbin/iodump

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf "$RPM_BUILD_ROOT"

%files
%defattr(-,root,root,-)
   /usr/sbin/iodump
   /usr/lib/iodump/
   /usr/src/os_health/iodump/kiodump.conf

%pre
ret=$(echo "%{all_kver}" | grep -w $(uname -r))
if [[ "${ret}str" == "str" ]];then
    echo "The current kernel version $(uname -r) is not supported by the current RPM package."
    exit 19
fi

%post
ps h -C iodump -o pid                        && killall iodump
[ -e /sys/module/kiodump/parameters/enable ] && echo N > /sys/module/kiodump/parameters/enable
lsmod | grep -qw kiodump                     && modprobe -r kiodump
kernel_ver=$(uname -r)
if [ -e "/usr/lib/iodump/kiodump-${kernel_ver}.ko" ];then
    install -d /lib/modules/${kernel_ver}/extra/os_health/iodump/
    install /usr/lib/iodump/kiodump-${kernel_ver}.ko /lib/modules/${kernel_ver}/extra/os_health/iodump/kiodump.ko
    /sbin/depmod -aeF /boot/System.map-${kernel_ver} ${kernel_ver} > /dev/null
    modprobe kiodump
    modprobe_ret=$?
    if [ $modprobe_ret -ne 0 ];then
        echo "kiodump.ko modprobe is failed, check the dmesg info."
        exit $modprobe_ret
    fi
else
    echo "The /usr/lib/iodump/kiodump-${kernel_ver}.ko file does not exist in the iodump rpm package"
    exit 2
fi
if [ -d /etc/modules-load.d/ ];then
    install /usr/src/os_health/iodump/kiodump.conf /etc/modules-load.d/kiodump.conf
fi

%preun
if [ "$1" = "0" ]; then
    ps h -C iodump -o pid                        && killall iodump
    [ -e /sys/module/kiodump/parameters/enable ] && echo N > /sys/module/kiodump/parameters/enable
    lsmod | grep -qw kiodump                     && modprobe -r kiodump
    kernel_ver=$(uname -r)
    rm -f /lib/modules/${kernel_ver}/extra/os_health/iodump/kiodump.ko
    /sbin/depmod -aeF /boot/System.map-${kernel_ver} ${kernel_ver} > /dev/null
    rm -f /etc/modules-load.d/kiodump.conf
fi

%changelog
* Wed Aug 24 2022 MilesWen <mileswen@linux.alibaba.com> - 1.0.1-1
- Release iodump RPM package
--end
