#!/bin/bash

deb_workspace=$(cd $(dirname $0);pwd)
current_kver=$(uname -r)
echo $deb_workspace

sourcedir=${deb_workspace}"/../"
builddir=${deb_workspace}"/.deb_create/BUILD/"
buildrootdir=${deb_workspace}"/.deb_create/BUILDROOT/"
debdir=${deb_workspace}"/.deb_create/DEBS/"

rm -fr .deb_create
mkdir -p $builddir
mkdir -p $buildrootdir
mkdir -p $debdir

# prep
install -d                        $builddir/user/
install -d                        $builddir/kernel/
install -v -D $sourcedir/user/*   $builddir/user/
install -v -D $sourcedir/kernel/* $builddir/kernel/
install -v    $sourcedir/Makefile $builddir/Makefile
install -d                        $builddir/debian/
install -v -D $sourcedir/debian/* $builddir/debian/

# make 
make -C $builddir

sed -i "s/%{current_kver}/${current_kver}/g"  $builddir/debian/control
sed -i "s/%{current_kver}/${current_kver}/g"  $builddir/debian/preinst 
sed -i "s/%{current_kver}/${current_kver}/g"  $builddir/debian/postinst
sed -i "s/%{current_kver}/${current_kver}/g"  $builddir/debian/prerm 

# install
install -d                             ${buildrootdir}/usr/lib/iodump/
install $builddir/kernel/kiodump.ko    ${buildrootdir}/usr/lib/iodump/kiodump.ko
install -d                             ${buildrootdir}/usr/src/os_health/iodump/
install $builddir/kernel/kiodump.conf  ${buildrootdir}/usr/src/os_health/iodump/kiodump.conf
install -d                             ${buildrootdir}/usr/sbin/
install $builddir/user/iodump          ${buildrootdir}/usr/sbin/iodump

# install plus
install -d                             ${buildrootdir}/DEBIAN/
install $builddir/debian/control       ${buildrootdir}/DEBIAN/
install $builddir/debian/preinst       ${buildrootdir}/DEBIAN/
install $builddir/debian/postinst      ${buildrootdir}/DEBIAN/
install $builddir/debian/prerm         ${buildrootdir}/DEBIAN/

# build
package_name=$(grep -F Package ${buildrootdir}/DEBIAN/control | head -n 1 | awk -F: '{print $2}')
version_name=$(grep -F Version ${buildrootdir}/DEBIAN/control | head -n 1 | awk -F: '{print $2}')
architecture_name=$(grep -F Architecture ${buildrootdir}/DEBIAN/control | head -n 1 | awk -F: '{print $2}')

package_name=$(echo $package_name)
version_name=$(echo $version_name)
architecture_name=$(echo $architecture_name)

dpkg-deb --build ${buildrootdir} ${debdir}/${package_name}"_"${version_name}"_"${architecture_name}".deb"

# bakup
cp ${debdir}/${package_name}"_"${version_name}"_"${architecture_name}".deb" ${deb_workspace}/
