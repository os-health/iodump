## iops dump使用手册

[English](README_en-us.md) | 中文

## 目录

  - 一、[工具介绍](#工具介绍)
  - 二、[工具特点](#工具特点)
  - 三、[工具安装](#工具安装)
    - 3.1、[rpm包打包方法1](#rpm包打包方法1)
    - 3.2、[rpm包打包方法2](#rpm包打包方法2)
    - 3.3、[rpm包打包方法3](#rpm包打包方法3)
    - 3.4、[deb包打包方法](#deb包打包方法)
    - 3.5、[源代码编译安装](#源代码编译安装)
  - 四、[使用说明](#使用说明)
    - 4.1、[基本使用](#基本使用)
    - 4.2、[参数说明](#参数说明)
    - 4.3、[长期测试](#长期测试)
  - 五、[输出说明](#输出说明)
  - 六、[结果有效性](#结果有效性)
    - 6.1、[数据有效性](#数据有效性)
    - 6.2、[跟踪点的正确选择](#跟踪点的正确选择)
    - 6.3、[再谈数据有效性](#再谈数据有效性)
  - 七、[使用案例](#使用案例)
    - 7.1、[诊断磁盘io打满](#诊断磁盘io打满)
    - 7.2、[内存颠簸型磁盘io打满](#内存颠簸型磁盘io打满)
    - 7.3、[诊断元数据io](#诊断元数据io)
    - 7.4、[写类型的元数据io](#写类型的元数据io)
    - 7.5、[获取元数据io的文件信息](#获取元数据io的文件信息)
    - 7.6、[读类型的元数据io](#读类型的元数据io)
    - 7.7、[写系统调用触发元数据读io](#写系统调用触发元数据读io)
    - 7.8、[存储io的双峰模式](#存储io的双峰模式)
  - 八、[性能开销](#性能开销)
  - 九、[许可证](#许可证)
  - 十、[技术交流](#技术交流)



<a name="工具介绍" ></a>

## 一、工具介绍

&emsp;&emsp;iops dump工具是利用内核tracepoint静态探针点技术实现的一个io问题排查工具。通过iops dump工具，我们可以获取每一个IOPS（w/s和r/s）的详细信息，不仅包括IO请求的size大小，还包括IO请求的扇区地址，同时还包含IO请求的发生时间、读写的文件全路径、产生IO请求的进程、产生IO请求的系统调用和扩展IO类型等信息。这其中最具有特色的就是读写的文件全路径功能。为了便于向大家介绍iops dump工具，下文将我们简称其为iodump。

<a name="工具特点" ></a>

## 二、工具特点

&emsp;&emsp;当我们要排查操作系统磁盘IO问题时，可以使用iostat扩展命令进行具体分析。当iostat工具显示此时磁盘IO并发很高，磁盘使用率接近饱和时，还需要依赖更多的工具进一步查看影响磁盘IO使用率高的进程信息和读写文件信息。

&emsp;&emsp;常见的工具或者方法有iotop、blktrace、ftrace和block_dump等。实际使用中，它们都有各种不足：
- iotop工具，可以细化到进程带宽信息，但缺乏进程级的IOPS信息，也没有对应的磁盘分区信息。
- blktrace工具，功能强大，但使用较复杂。获取sector信息后，进一步通过debugfs等其他方式解析文件路径也比较低效。
- ftrace工具，当跟踪块设备层静态探针点时，功能和blktrace工具类似，也需要通过debugfs等工具进一步解析文件路径。当跟踪文件系统层探针点函数时，无法精确对应IOPS数量。
- block_dump工具，也同样存在以上ftrace工具的2个不足。

&emsp;&emsp;**相比其他磁盘io类工具，iodump有如下几个特色优势** ：
1. 支持自定义选择blk层探针点函数。
2. 支持自定义输出字段信息，包括时间、进程名、进程ID、IO大小、扇区地址、磁盘分区、读写类型、扩展IO类型、IO来源、Inode号，文件全路径。
3. 当采集进程异常退出后，支持内核态自动关闭探针。
4. 支持从2.6.32以上的各内核版本。
5. 当IOPS高时，支持抽样输出。

&emsp;&emsp;iodump功能虽然强大，<font color=red>但iodump本质上采用的是加载内核模块方式实现，可能会引起操作系统crash，请在重要的生产环境使用前，提前进行充分测试</font>。从计算平台的实际使用情况，未发生生产环境宕机。

<a name="工具安装" ></a>

## 三、工具安装

&emsp;&emsp;工具的安装，有如下几种方法:

<a name="rpm包打包方法1" ></a>

### 3.1、rpm包打包方法1

&emsp;&emsp;rpm包的打包方法如下，本方法适用于AnolisOS and CentOS等操作系统环境。

```bash
$ yum install rpm-build rpmdevtools git
$ cd /tmp/                                        # work dir
$ git clone https://gitee.com/anolis/iodump.git
$ rpmdev-setuptree
$ tar -zcvf ~/rpmbuild/SOURCES/iodump-$(cat iodump/spec/iodump.spec |grep Version |awk '{print $2}').tar.gz iodump
$ cp iodump/spec/iodump.spec ~/rpmbuild/SPECS/
$ rpmbuild -bb ~/rpmbuild/SPECS/iodump.spec
$ cd ~/rpmbuild/RPMS/x86_64/
$ sudo rpm -ivh $(ls)
$ sudo rpm -e iodump-$(uname -r)                  # remove package
```

&emsp;&emsp;iodump工具本质上是内核驱动模块，在一个特定内核版本上生成的rpm包在另外一个不同的内核版本上是不能正常工作的。在这里我们将内核版本信息加入到rpm包的name部分，用以识别不同内核版本的rpm包。这里我们推荐使用如下rpm的查询命令区分一个rpm包的name、version、release和arch四个部分的内容。我们使用连续的3横线来区隔不同的部分，结果一目了然。

```bash
$ rpm -qp iodump-4.19.91-24.8-1.0.1-1.an8.x86_64.rpm --queryformat="%{name}---%{version}---%{release}---%{arch}\n"
iodump-4.19.91-24.8---1.0.1---1.an8---x86_64
```

&emsp;&emsp;使用这个方法打rpm，默认会依赖当前机器上默认第一个kernel-devel包。如果想指定版本的kernel-devel包，可以使用如下rpmbuild命令通过宏参数传入内核版本信息。

```bash
$ rpmbuild -bb ~/rpmbuild/SPECS/iodump.spec --define "%kver $(uname -r)"
$ rpmbuild -bb ~/rpmbuild/SPECS/iodump.spec --define "%kver 4.19.91-24.8.an8.x86_64"
```

<a name="rpm包打包方法2" ></a>

### 3.2、rpm包打包方法2

&emsp;&emsp;有些情况下，在一个发行版下面，迭代了很多版本的内核，本方法适用于这种场景。

&emsp;&emsp;这里以Anolis 8为例，有Anolis8.2、8.4和8.6等多个小的发行版本，每个小版本也分别对应了不同的内核版本。

> Release&emsp;&emsp;&emsp;&emsp;&emsp;Kernel Version

> 8.2 ANCK 64位&emsp;&emsp;4.19.91-25.8.an8.x86_64

> 8.4 ANCK 64位&emsp;&emsp;4.19.91-26.an8.x86_64

> 8.6 ANCK 64位&emsp;&emsp;4.19.91-26.1.an8.x86_64

&emsp;&emsp;此时，新版本的gcc版本通常也更新，会对低版本向下兼容。因此，我们选择最新版本Anolis8.6作为打包机。并且安装上所有需要支持的内核版本对应的kernel-devel的rpm包。

> Release&emsp;&emsp;&emsp;&emsp;&emsp;Kernel-devel

> 8.2 ANCK 64位&emsp;&emsp;kernel-devel-4.19.91-25.8.an8.x86_64

> 8.4 ANCK 64位&emsp;&emsp;kernel-devel-4.19.91-26.an8.x86_64

> 8.6 ANCK 64位&emsp;&emsp;kernel-devel-4.19.91-26.1.an8.x86_64

&emsp;&emsp;相关版本的kernel-devel包，我们推荐在阿里巴巴开源镜像站进行搜索查找并下载。

> https://developer.aliyun.com/packageSearch

&emsp;&emsp;具体打包方案如下：

```bash
$ rpm -ivh --force kernel-devel-4.19.91-25.8.an8.x86_64.rpm kernel-devel-4.19.91-26.an8.x86_64.rpm
$ yum install rpm-build rpmdevtools git
$ cd /tmp/                                        # work dir
$ git clone https://gitee.com/anolis/iodump.git
$ rpmdev-setuptree
$ tar -zcvf ~/rpmbuild/SOURCES/iodump-$(cat iodump/spec/distribution.spec |grep Version |awk '{print $2}').tar.gz iodump
$ cp iodump/spec/distribution.spec ~/rpmbuild/SPECS/
$ rpmbuild -bb ~/rpmbuild/SPECS/distribution.spec
$ cd ~/rpmbuild/RPMS/x86_64/
$ rpm -qpl $(ls) | grep kiodump                   # display all version kiodump 
$ sudo rpm -ivh $(ls)
$ sudo rpm -e iodump                              # remove package
```

<a name="rpm包打包方法3" ></a>

### 3.3、rpm包打包方法3

&emsp;&emsp;在一些大型互联网公司，会在低版本发行版上使用较高版本内核的情况。比如centos7的原装内核版本是3.10，但是为了支持业务需求，在centos7的发行版上将内核升级为更高版本，如4.18内核。本方法适用于这种场景

&emsp;&emsp;这里以centos7为例，假设这里当前某公司同时使用了6个版本的内核，分别是3.10.0-1062、3.10.0-1127、3.10.0-1160、4.18.0-240、4.18.0-305和4.18.0-348。我们需要基于centos7发行版，制作2个docker镜像。

&emsp;&emsp;第一个docker镜像中包含3个3.10内核版本，需要安装如下软件包，命名为image-centos7。

```bash
$ yum install rpm-build
$ rpm -ivh --force kernel-devel-3.10.0-1062.el7.x86_64.rpm kernel-devel-3.10.0-1127.el7.x86_64.rpm kernel-devel-3.10.0-1160.el7.x86_64.rpm
```

&emsp;&emsp;第二个docker镜像中包含3个4.18内核版本，需要安装如下软件包，命名为image-centos8。

```bash
$ yum install rpm-build
$ rpm -ivh --force kernel-devel-4.18.0-240.el8.x86_64.rpm kernel-devel-4.18.0-305.el8.x86_64.rpm kernel-devel-4.18.0-348.el8.x86_64.rpm
$ rpm -Uvh gcc-8.5.0-4.el8_5.x86_64.rpm                        # 具体需要升级的gcc相关rpm包参考公司内部资料
```

&emsp;&emsp;然后，具体打包方案如下：

```bash
$ yum install rpmdevtools git docker
$ rpmdev-setuptree
$ cd ~/
$ git clone https://gitee.com/anolis/iodump.git
$ tar -zcvf ~/rpmbuild/SOURCES/iodump-$(cat iodump/spec/docker.spec |grep Version |awk '{print $2}').tar.gz iodump
$ cp iodump/spec/docker.spec ~/rpmbuild/SPECS/
$ docker run --net=host -u root -v ~/rpmbuild:/root/rpmbuild -w /root/rpmbuild -it image-centos8 /bin/bash -c "rpmbuild -bb /root/rpmbuild/SPECS/docker.spec"
$ docker run --net=host -u root -v ~/rpmbuild:/root/rpmbuild -w /root/rpmbuild -it image-centos7 /bin/bash -c "rpmbuild -bb /root/rpmbuild/SPECS/docker.spec"
$ rm -fr ~/rpmbuild/SPECS/kiodump/                             # clear kiodump.ko
$ cd ~/rpmbuild/RPMS/x86_64/
$ rpm -qpl iodump-*.an8.x86_64.rpm | grep kiodump              # display all version kiodump 
$ sudo rpm -ivh iodump-*.an8.x86_64.rpm
$ sudo rpm -e iodump                                           # remove package
```

<a name="deb包打包方法" ></a>

### 3.4、deb包打包方法

&emsp;&emsp;deb包的打包方法如下，本方法适用于Ubuntu等操作系统环境。

```bash
$ apt-get update
$ apt install git
$ cd ~/
$ git clone https://gitee.com/anolis/iodump.git
$ cd iodump/debian
$ ./build.sh
$ dpkg -i iodump-4.4.0-87-generic_1.0.1-1_amd64.deb
$ dpkg -r iodump-4.4.0-87-generic                              # remove package
```

<a name="源代码编译安装" ></a>

### 3.5、源代码编译安装

&emsp;&emsp;源代码安装方法。

```bash
$ cd ~/
$ git clone https://gitee.com/anolis/iodump.git
$ cd iodump
$ make
$ sudo make install
$ sudo make uninstall                                          # remove
```

<a name="使用说明" ></a>

## 四、使用说明

<a name="基本使用" ></a>

### 4.1、基本使用

&emsp;&emsp;基本运命命令如下。并且运行时需要使用sudo进行提权。

```bash
$ sudo iodump -p sda
```

&emsp;&emsp;结束运行iodump，可以在运行过程中直接使用键盘的ctrl+c的组合，或在另外一个终端运行如下命令。

```bash
$ sudo killall iodump
$ sudo killall -9 iodump
```

&emsp;&emsp;当向iodump进程发送SIGHUP、SIGINT和SIGTERM等信号时，用户态的iodump进程会在退出前向内核模块发送关闭内核跟踪的消息。但是，当向iodump进程发送-9，即SIGKILL信号时，用户态进程iodump将强制退出，无法向内核模块发送关闭内核跟踪的消息。此时，kiodump内核模块会在用户态iodump进程异常退出后，自动关闭内核跟踪开关。其它的blktrace等工具并没有此功能。

<a name="参数说明" ></a>

### 4.2、参数说明

&emsp;&emsp;为了完整的了解使用方法，我们可以查看帮助信息。

```bash
$ iodump -h

Usage: iodump [OPTIONS] 

Summary: this is a io tools. it can dump the details of struct request or struct bio.

Options:
  -h             Get the help information.
  -H             Hiding header information.
  -a <G>         Set blk tracepoint action which is fully compatible with blktrace, default G, See Actions.
  -o <pid,comm>  Set the output field, such as datetime,comm,pid, See Formats.
  -p <sda2>      Set partition parameter, this option is necessary.
  -s <filepath>  Set saving output to the file. if not set, it will output to standard output.
  -t <time>      Set tracing last time, default last time is 300 second, default 300 seconds.
  -O <ino>       Set the extra output field, such as tid,ino, See Formats.
  -S <number>    Set sample number, Only 1/number output is displayed, default 1.
  -c <comm>      Just output exact match comm string record.
  -C <comm>      Just output record which comm contain the comm string.
  -P <pid>       Just output exact match pid record.

Major Actions:
   ACTION  TRACEPOINT             UNIT
   Q       block_bio_queue        bio
   G       block_getrq            bio
   I       block_rq_insert        request
   D       block_rq_issue         request
   C       block_rq_complete      request
Minor Actions:
   B       block_bio_bounce       bio
   F       block_bio_frontmerge   bio
   M       block_bio_backmerge    bio
   S       block_sleeprq          bio
   X       block_split            bio
   R       block_rq_requeue       request

Formats:
   FIELD          DESCRIPTION
   datetime       Such as 2022-03-23T16:42:05.315695, Precision millisecond.
   timestamp      Such as 1648025082259168, Precision millisecond.
   comm           Such as iodump, process short name.
   pid            tgid 
   tid            task id
   iosize         IO size, the unit is byte.
   sector         Sector address on the disk.
   partition      Such as sda5.
   rw             The value list is R(read) or W(write).
   rwsec          The value list is A(ahead) E(secure) F(force unit access) M(meta) S(sync) V(vacant)
   launcher       The bottom function of the call stack.
   ino            Inode number.
   fullpath       Read or Write file full path.
```

&emsp;&emsp;下面介绍各主要参数的含义：
* -p参数： 设置需要追踪的磁盘或磁盘分区，例如 -p sda 或 -p sda5。这个参数是必选参数，不设置程序运行报错。
* -t参数： 设置追踪程序运行的时长，例如 -t 60 或-t -1，此时程序运行60秒后会自动终止，-1表示永远运行。不指定-t参数，程序默认运行300秒结束。
* -s参数： 设置追踪信息存储的文件，例如 -s /tmp/log。不指定-s参数时，追踪信息会打印到屏幕标准输出，此时也可以通过重定向将追踪信息保存到磁盘文件。
* -S参数： 设置抽样输出的比例。例如 -S 30，此时iodump将每隔30次只输出一次。
* -H参数： 设置屏蔽标题栏信息的输出，例如 -H。
* -a参数： 设置iodump追踪的内核静态探针点，选项值可以是5个主要跟踪点Q、G、I、D、C，以及其他几个辅助跟踪点B、F、M、R、S和X。默认缺省为G，即block_getrq，UNIT为bio。其他一些探针点UNIT也可能是request。
* -o参数： 设置输出信息字段，例如 -o pid,comm。字段值可以是datetime、timestamp、comm、pid、tid、iosize、sector、partition、rw、rwsec、launcher、ino和fullpath，多选逗号隔开。
* -O参数： 设置额外追加的输出信息字段，例如 -O tid,ino。默认输出信息字段组合为datetime,comm,pid,iosize,rw,rwsec,launcher,fullpath。
* -c参数： 仅输出和输入的进程名精准匹配的结果，例如，-c kworker/1:0.
* -C参数： 仅输出和输入的进程名模糊匹配的结果，例如，-C kworker.
* -P参数： 仅输出和输入的PID精准匹配的结果，例如，-P 1234.

&emsp;&emsp;下面是一些各种参数组合运行的实例。

```bash
$ sudo iodump -p sda
$ sudo iodump -p sda5 -t -1 -H >/tmp/log
$ sudo iodump -p nvme -t 600 -H -s /tmp/log
$ sudo iodump -p nvme0n1p1 -S 10
$ sudo iodump -p sda -a G
$ sudo iodump -p sda -o comm,pid
$ sudo iodump -p sda -O tid,ino
$ sudo iodump -p sda -c kworker/1:0
$ sudo iodump -p sda -C kworker
$ sudo iodump -p sda -P 1234
```

<a name="长期测试" ></a>

### 4.3、长期测试

&emsp;&emsp;上面提到iodump由内核驱动模块实现，可能引发宕机。因此在重要场景使用前需要进行充分的测试。这里提供一个测试命令。

```bash
$ sudo iodump -p sda -t 8640000 > /dev/null 2>/dev/null &
```

<a name="输出说明" ></a>

## 五、输出说明

&emsp;&emsp;iodump的每一行输出信息代表一个io请求（request结构体或bio结构体）的详细信息。iodump的输出信息，如所示。

```bash
$ iodump -p sda
datetime                   comm                pid iosize partition rw rwsec fullpath
2022-04-26T22:53:18.272487 kworker/u128:0    19607 131072 sda3       W     V /var/log/messages
2022-04-26T22:53:18.272563 jbd2/sda3-8         834   4096 sda3       W    FS -
2022-04-26T22:53:23.392466 kworker/u128:2    19494  16384 sda3       W     M -
datetime                   comm                pid iosize partition rw rwsec fullpath 
```

&emsp;&emsp;这些输出信息现在已经有9列数据项，其中每一项的含义说明如下：

* datetime： 日期时间格式，小数点后是微秒信息。
* timestamp：时间戳信息，单位微秒（us）。
* comm：     上下文环境中的进程名。
* pid：      上下文环境中的进程ID。
* tid：      上下文环境中的线程ID。
* iosize：   一次发向磁盘的io的数据大小，例如4096、524288等，单位为字节，数值必须是4096的倍数，4096是一个page的大小。
* sector：   一次发向磁盘的request的数据在磁盘中的扇区地址。扇区地址是硬盘出厂时，低级格式化时的一个扇区顺序号。一个扇区地址在一块磁盘中是唯一的。
* partition：一次发向磁盘的request的数据所在磁盘分区信息，例如sda5，sda，nvme0n1p3和nvme0n1。
* rw：       IO基本类型，R是READ，W是WRITE，D是DISCARD，E是SECURE_ERASE和DISCARD，F是FLUSH，N是Other，值单选。
* rwsec：    IO扩展类型，F是FUA(forced unit access)，A是RAHEAD(read ahead)，S是SYNC，M是META，E是SECURE。可多选，以上都没有时，显示V。
* launcher： 对于从用户态发起的IO调用栈，这里将显示出系统调用名称信息。
* ino：      一次发向磁盘的request的数据在磁盘分区中的inode号信息，有些操作元数据信息的request请求这个inode信息为0。
* fullpath： 如果inode信息不为0时，所对应的文件名以及其磁盘路径。这里解析了所在磁盘分区的完整文件路径。当磁盘IO打满时，我们可以通过对FilePath字段的分析，迅速定位问题。

<a name="结果有效性" ></a>

## 六、结果有效性

&emsp;&emsp;磁盘io打满时，io诊断工具尽可能的跟踪到每一个iops信息并且输出，只有这样才能说明这个工具是有效的。如果只能显示其中的20-30%，那工具的价值将是大打折扣的。

<a name="数据有效性" ></a>

### 6.1、数据有效性

&emsp;&emsp;日常我们判断一个磁盘分区iops是否打满主要依赖iostat工具，该工具数据源取自/proc/diskstats伪文件。我们可以使用如下脚本对iodump的输出结果和iostat的结果进行对比。

```bash
$ cat io_compare.sh 
#!/bin/bash
if [ -z "$1" ];then
    echo -e "disk partition need to input, such as \n   ./io_compare.sh sda5"
    exit 0
fi
partition=$1
echo "Wait 60s for iops comparison..."
sudo iodump -H -p $partition -t 60 > /tmp/iodump.log & 
iostat -x -d $partition 1 60 | grep $partition > /tmp/iostat.log &
wait
iodump_read=$(cat /tmp/iodump.log | grep -w R | wc -l)
iodump_write=$(cat /tmp/iodump.log | grep -w W | wc -l)
iostat_read=$(cat /tmp/iostat.log  | awk 'BEGIN{sum=0}{sum=sum+$4}END{print int(sum)}')
iostat_write=$(cat /tmp/iostat.log  | awk 'BEGIN{sum=0}{sum=sum+$5}END{print int(sum)}')
iodump_total=$((iodump_read+iodump_write))
iostat_total=$((iostat_read+iostat_write))
read_percent=$(echo "$iodump_read $iostat_read" | awk '{div=100*$1/$2;printf("%.2f%%",div)}')
write_percent=$(echo "$iodump_write $iostat_write" | awk '{div=100*$1/$2;printf("%.2f%%",div)}')
total_percent=$(echo "$iodump_total $iostat_total" | awk '{div=100*$1/$2;printf("%.2f%%",div)}')
echo "partition $partition results:"
result="Item            Read         Write         Total"
result=$result"\niostat $iostat_read $iostat_write $iostat_total"
result=$result"\niodump $iodump_read $iodump_write $iodump_total"
result=$result"\npercent $read_percent $write_percent $total_percent"
echo -e "$result" | column -t
```

&emsp;&emsp;运行iops对比脚本，结果如下。

```bash
$./io_compare.sh nvme0n1p1
Wait 60s for iops comparison...
partition nvme0n1p1 results:
Item     Read    Write   Total
iostat   5122    35660   40782
iodump   5044    35394   40438
percent  98.48%  99.25%  99.16%
```

&emsp;&emsp;从对比结果可以看到iodump采集到的iops数量在读和写上，都非常接近iostat的统计值。只有这样，当磁盘io打满时，我们分析iodump的结果，才是有意义的。

<a name="跟踪点的正确选择" ></a>

### 6.2、跟踪点的正确选择

&emsp;&emsp;iodump的-a选项提供了Q、G、I、D和C几个主要的跟踪点参数，按照顺序，这也是磁盘io在内核块设备层的路径顺序，iodump默认选择使用G跟踪点。但也有一些磁盘类型，并不经过G跟踪点，此时我们可以改用Q跟踪点。这类磁盘有个共同特点，iostat的rrqm/s和wrqm/s都永远为0。

```bash
$ sudo iodump -p dfa -a Q
```

<a name="再谈数据有效性" ></a>

### 6.3、再谈数据有效性

&emsp;&emsp;结合上文介绍，对于普通的磁盘类型，选择G跟踪点，iodump输出结果将在iops数量上和iostat对齐。如果选择了Q跟踪点，iodump输出结果将较大幅度多余iostat的结果，这部分多出来的就是rrqm/s和wrqm/s的数量。

&emsp;&emsp;但是选择G跟踪点时，iodump的输出结果在bps指标上会较大幅度小于iostat的结果，这是由于G跟踪点block_getrq只获取了新创建request结构体时的bio结构体信息，没有包含被merge合并的bio的io数据大小。此时，我们使用D和C跟踪点可以实现iodump和iostat在bps指标上一致的目的。

&emsp;&emsp;各个跟踪点各有优缺点，用如下表格显示。默认跟踪点选择G是一个权衡利弊的结果。

| 跟踪点 | iops    |  bps  | 上下文  |
| :---:      | :---:     |  :---:   | :---:       |
| Q         |  多于  |  等于 |  保持     |
| G         |  等于  |  少于 |  保持     |
| I           |  少于  |  少于 |  保持     |
| D         |  等于  |  等于 |  改变     |
| C         |  等于  |  等于 |  改变     |

&emsp;&emsp;实际使用中，iostat工具毕竟只能记录实时数据，有时候我们还需要依赖sar类型工具自动记录io活动的历史数据。在这里我们推荐龙蜥社区的ssar工具的tsar2命令。

<a name="使用案例" ></a>

## 七、使用案例

<a name="诊断磁盘io打满" ></a>

### 7.1、诊断磁盘io打满

&emsp;&emsp;生产中遇到一个案例，磁盘分区sda6平时读IOPS是20多，但有时候读IOPS会瞬时高出数倍，并将磁盘io util打满。通过查看tsar2显示历史数据如下。

```bash
$ tsar2 --io -i 1 -I sda6                 
Time            -sda6--  -sda6--   -sda6--
Time                 rs       ws      util
30/05/22-03:30    25.93    68.33     40.71
30/05/22-03:31    24.82    69.60     40.50
30/05/22-03:32    42.88    53.77     61.69
30/05/22-03:33   327.93    52.13    100.00
30/05/22-03:34   390.03    46.73    100.00
30/05/22-03:35   300.53    56.80    100.00
```

&emsp;&emsp;可以看到随着读iops（rs指标）的快速增长，磁盘使用率（util）指标瞬间打满到100%。磁盘使用率打满时，会严重影响应用进程的io请求响应时间。此时，iodump能详细显示每一个iops的详情信息，以便能判断高并发的iops是否符合预期。

```bash
$ sudo iodump -p sda6 -t 60 -s iodump.log       # 使用iodump抓取io详情
```

&emsp;&emsp;通过iodump工具抓取sda6分区的io detail信息，一目了然，非常明显的即可看到是task进程在大量读取rank.data这个archive的文件。以上案例也是iodump使用中最常用的经典场景。

```bash
$ cat iodump.log
datetime                    comm   ioize  rw  launcher  fullpath 
2022-06-01T14:24:58.163214  task    4096   R  read      /archive/rank.data
2022-06-01T14:24:58.163234  task    4096   R  read      /archive/rank.data
2022-06-01T14:24:58.163258  task   12288   R  read      /archive/rank.data
2022-06-01T14:24:58.163318  task    8192   R  read      /archive/rank.data
```

<a name="内存颠簸型磁盘io打满" ></a>

### 7.2、内存颠簸型磁盘io打满

&emsp;&emsp;有些情况下，我们会看到launcher字段是page_fault或async_page_fault，并且rw字段是R读。最重要的是，如果观察一个时间区间内（比如60秒）的 数据，我们会发现同一个扇区地址sector字段，会重复读取几百，几千或上万次。那么这种情况就是内存颠簸引起的读磁盘io。同时可能伴有iosize是4096等小io，fullpath的文件为binary文件等特征。

```bash
datetime                   comm    pid iosize    sector rw   launcher     fullpath
2022-06-01T19:52:44.200717 task  48309 4096   242275424  R   page_fault   /lib64/libmodx.so
2022-06-01T19:52:44.215145 task  48309 4096   242280736  R   page_fault   /lib64/libmodx.so
2022-06-01T19:52:44.236601 task  48309 4096   242278216  R   page_fault   /lib64/libmodx.so
```

&emsp;&emsp;字段launcher为page_fault，说明io产生的原因是发生了主缺页中断。一次主缺页中断代表程序执行代码段时，内存中不存在这块代码段，进而引起一次读IO，从磁盘中读取这部分代码段内容。内存颠簸时就会发生内存中频繁且反复出现代码段不存在的情况。

&emsp;&emsp;引起内存颠簸可能是单cgroup层面、单numa节点或整机层面内存紧张引起。当通过上述特征判断出是内存颠簸引起的读io打满时，以单cgroup内存紧张为例，pid是48309，进一步排查方法如下。

```bash
$ cat /proc/48309/cgroup | grep memory
9:memory:/system.slice/sshd.service
$ cat /sys/fs/cgroup/memory/system.slice/task.service/memory.limit_in_bytes 
8615100416
$ ps h -p  48309 -o rss  
8240736
$ echo $(((8615100416/1024-8240736)/1024))
168
```

&emsp;&emsp;进程的rss内存是8240736kB，此时这个进程的cgroup的memory.limit_in_bytes是8615100416。
说明在cgroup的限额内，已经被rss占据了绝大部分配额，只留下168Mb配额，不足以完全融下进程的代码段，从而引起代码段的不同部分频繁被丢弃和磁盘读取。

<a name="诊断元数据io" ></a>

### 7.3、诊断元数据io

&emsp;&emsp;块设备层产生的io，大体上可以分为2大类。第一类是由于读写用户数据产生数据io，又叫block io。另外一类是读写文件的各种属性信息等的元数据io，又叫meta io。对于block io，iodump目前可以解析出读写的文件全路径，显示在fullpath列，但是对于meta io，iodump目前尚没有实现低开销情况下解析完整路径的功能。这种meta类型的io，即使没有完整的fullpath文件信息，我们仍然可以通过iodump提供的其他字段获取有价值的重要信息，比如进程名字段comm和包含系统调用信息的launcher字段。

&emsp;&emsp;严格意义上说，在fullpath字段未能解析的情况下，还有一种文件系统本身记录journal log产生的io。这种类型的io典型特征是comm字段以jbd2开头，后面是磁盘分区的信息。jbd类型的io是由于其他写类型io间接引发，本身iops数量占比也不高。

```bash
datetime                   comm          iosize   rw rwsec launcher       fullpath
2022-06-02T19:32:28.441106 jbd2/vdc2-8     4096    W     S ret_from_fork  -
2022-06-02T19:32:28.441465 jbd2/vdc2-8     4096    W     S ret_from_fork  -
2022-06-02T19:32:28.441482 jbd2/vdc2-8     4096    W     S ret_from_fork  -
```

<a name="写类型的元数据io" ></a>

### 7.4、写类型的元数据io

&emsp;&emsp;还有一类io，典型特征是comm字段以kworker开头，fullpath字段未解析，扩展io类型中显示这个io扩展类型为M，即Meta类型。kworker是内核线程，说明这些写操作经过了内核pagecache。符合以上特征的io，根据我们的经验主要是由unlink/unlinkat、rename/renameat、mkdir/mkdirat和open/openat（O_CREAT）等系统调用触发。

```bash
datetime                   comm           iosize rw rwsec launcher       fullpath
2022-06-01T16:47:35.768009 kworker/u200:1   4096  W     M ret_from_fork  -       
2022-06-01T16:47:35.768018 kworker/u200:1   4096  W     M ret_from_fork  -       
2022-06-01T16:47:35.768027 kworker/u200:1   4096  W     M ret_from_fork  -       
```

<a name="获取元数据io的文件信息" ></a>

### 7.5、获取元数据io的文件信息

&emsp;&emsp;元数据写类型io的三个关键字段comm、launcher和fullpath都没有有价值的信息，无法有效定位io来源。但是这种类型的io，我们可以通过系统调用层的跟踪来准确定位io来源。这里提供两种有效方案。

&emsp;&emsp;下面是一个用于追踪rename系统调用的bcc-tools，及使用它追踪文件改名操作的效果。必要时，也可以根据进程名称或者pid进行过滤。

```bash
$ sudo /usr/share/bcc/tools/trace 'sys_rename "%s %s", arg1, arg2'
PID     TID     COMM     FUNC              -
136355  139335  task     sys_unlink       /snapshot/status.2.LOG /snapshot/status.3.LOG
136355  139335  task     sys_unlink       /snapshot/status.1.LOG /snapshot/status.2.LOG
136355  139335  task     sys_unlink       /snapshot/status.LOG /snapshot/status.1.LOG
```

&emsp;&emsp;对于较低版本内核，可以使用systemtap工具。下面是一个用于追踪rename系统调用的systemtap脚本，及使用追踪脚本追踪文件改名的效果。必要时，可以根据进程名称或者pid进行过滤。

```bash
$ cat trace.stp 
#!/usr/bin/env stap
probe syscall.rename{
    printf("%s %s %d %s\n", name, execname(), pid(), argstr);
}
probe timer.s(60){
    exit();
}

$ sudo stap trace.stp
rename task 11669 "/snapshot/status.2.LOG /snapshot/status.3.LOG"
rename task 11669 "/snapshot/status.1.LOG /snapshot/status.2.LOG"
rename task 11669 "/snapshot/status.LOG /snapshot/status.1.LOG"
```

&emsp;&emsp;以上这种日志轮转引起的写类型的元数据io是一种生产上常见的引起大量磁盘io的原因。

<a name="读类型的元数据io" ></a>

### 7.6、读类型的元数据io

&emsp;&emsp;除去以上谈到的jbd2和kworker外，其他产生io的进程主要是用户态进程。由用户态进程引发的写入类型io大部分情况下都会经过pagecache，因此观察到的用户态进程触发的io主要是读类型io。进一步识别，需要主要关注launcher字段。

&emsp;&emsp;这类元数据类型的读io主要是由getdents/getdents64、stat、lstat、fstat/fstatat和access/faccessat等系统调用触发。除getdents/getdents64外，大多可以通过上文提供的跟踪系统调用方式找到io来源。

```bash
datetime                   comm   iosize rw rwsec launcher    fullpath
2022-06-01T15:36:21.313887 task    4096  R     M  getdents    -
2022-06-01T15:36:21.314465 task    4096  R     A  newstat     -
2022-06-01T15:36:21.314489 task    4096  R     A  newlstat    -
2022-06-01T15:36:21.314507 task    4096  R     A  newfstat    -
```

<a name="写系统调用触发元数据读io" ></a>

### 7.7、写系统调用触发元数据读io

&emsp;&emsp;在实际跟踪io活动时，我们会发现如unlink、rename这些写入元数据的系统调用，会触发读io。这类io主要是由于进程执行这类系统调用，需要识别这个文件时，把文件路径名传递给内核。这个过程需要不断检查与每个目录或文件向匹配的目录项数据结构，这就需要触发从磁盘的元数据读操作。

```bash
datetime                   comm   iosize rw rwsec launcher    fullpath
2022-06-03T18:25:02.768859 task   4096   R   M    rename      -
2022-06-03T18:25:02.944608 task   4096   R   M    unlink      -
2022-06-03T18:25:02.944780 task   4096   R   M    mkdir       -
```

&emsp;&emsp;不论是读系统调用触发的元数据读io，还是写系统调用触发的元数据读io，当内存资源相对充裕时，内核buffer cache都会将曾经访问过的元数据信息都缓存住。一旦出现内存资源相对不足时，buffer cache就会被内核回收。这时用户态程序再次访问这部分元数据信息时，就会触发大量读io。

<a name="存储io的双峰模式" ></a>

### 7.8、存储io的双峰模式

&emsp;&emsp;内核有个参数max_sectors_kb，表示设备允许的最大请求大小，设置太大会超过磁盘硬件的允许范围。在这里，我们设置为512KB。操作系统的块设备层会将较大的数据切分为不大于512KB的request请求。

```bash
$ cat  /sys/block/sdc/queue/max_sectors_kb
512
```

&emsp;&emsp;然后我们使用iodump抓取60秒的数据，并对写数据进行分析。

```bash
$ sudo iodump -p sdc1 -t 60 -s iodump.log -H
$ cat iodump.log | grep -w W | awk '{print $4}' | sort | uniq -c | sort -k 2nr
   2245 524288
     24 520192
      6 516096
......
      2 16384
     19 12288
     35 8192
   1248 4096
```

&emsp;&emsp;明显可以看到，所有的写io的iosize大小呈现双峰分布，一部分集中于4096大小，一部分集中于524288的大小。这和我们前面的提到的场景是吻合的，iosize是4096大小的基本属于元数据io，iosize是524288大小的基本属于数据块io。换算成秒级数据，524288大小的io数为37.4个/秒，4096大小的io数为20.8个/秒。

&emsp;&emsp;在这里，我们查看iodump运行抓取相同时间的60秒的tsar2的io监控数据。ws表示写iops数为64.23，warqsz表示平均写io的大小为321.28Kb。

```bash
$  tsar2 --io -I sdc1 -i 1
Time           -sdc1-- -sdc1-- 
Time                ws  warqsz 
04/06/22-09:53   64.23  321.28 
```

&emsp;&emsp;基于iosize的双峰分布模式，我们可以简单的列一个二元一次方程。x是4096这样的小io的数量，y是524288这样的大io的数量。

```bash
x + y = 64.23
4096 * x + 524288 * y = 64.23 * 321.28 * 1024
```

&emsp;&emsp;解析二元一次方程，x是24.11，y是40.12。对比iodump实际抓取的小iops数占比35.8%，和根据tsar2监控数据推算的小iops数占比37.5%，基本相当。这说明，我们平时通过各种监控系统看到的平均io大小指标中蕴含重要信息。

| 数据源    | iodump |  tsar2   |
| :---:         | :---:        |  :---:     |
| 大io数     |  37.4     |  40.12  |
| 小io数     |  20.8     |  24.11  | 
| 小io占比 |  35.8%  |  37.5% | 

<a name="性能开销" ></a>

## 八、性能开销

&emsp;&emsp;说到性能开销，需要先简单介绍下目前行业内各种主流的trace工具的技术实现原理。一般都是在进程上下文环境的tracepoint钩子函数中向一个ring buffer中写入数据，另外一个用户态进程再从ring buffer中读取数据。这样性能开销就需要分两部分计量。

&emsp;&emsp;我们构造了一个每秒1.2万读iops和250写iops的测试环境，开启iodump后，CPU性能开销如下。

| -                     |  CPU            |   MEM      |
| :---:                | :---:                |  :---:         |
| iodump进程   |  单核0.3%     |  876KB    |
| 1比100抽样   |  单核1.6%     |  1MB/核   | 
| 全量采集       |  单核10%       |  1MB/核   | 


<a name="许可证" ></a>

## 九、许可证

&emsp;&emsp;This project is licensed under the MIT License - see the [LICENSE](./LICENSE) file for details, unless explicitly stated otherwise. Some files in the 'kernel' directory are dual licensed under either GPL v2 or MIT. Relevant license is reminded at the top of each source file.

<a name="技术交流" ></a>

## 十、技术交流

&emsp;&emsp;iops dump工具还在不断开发和优化过程中，如果大家觉得工具使用有任何疑问、对工具功能有新的建议，或者想贡献代码，请加群交流和反馈信息。钉钉群号：31987277 或 33304007。
