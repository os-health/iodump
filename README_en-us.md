## iops dump

English | [中文](README.md)

## Table of Contents

  - 1. [Introduction](#Introduction)
  - 2. [Advantage](#Advantage)
  - 3. [Installation](#Installation)
    - 3.1. [AnolisOS CentOS 1](#AnolisOS-CentOS1)
    - 3.2. [AnolisOS CentOS 2](#AnolisOS-CentOS2)
    - 3.3. [Ubuntu](#Ubuntu)
    - 3.4. [Source Code](#Source-Code)
  - 4. [instructions](#instructions)
    - 4.1. [basic usage](#basic-usage) 
    - 4.2. [option usage](#option-usage)
  - 5. [output format](#output-format)
  - 6. [LICENSE](#LICENSE)
  - 7. [get support](#get-support)

<a name="Introduction"></a>

## 1. Introduction

IOPS dump is an I/O troubleshooting tool that uses the kernel Tracepoint static probe point technology. Using the IOPS dump tool, you can obtain detailed information about each IOPS (W/S and R/s), including the size and sector address of the I/O request. In addition, the information includes the occurrence time of the I/O request, full path of the read/write file, process that generates the I/O request, system call that generates the I/O request, and extended I/O type. The most characteristic of these is the full path function of reading and writing files. To introduce the IOPS dump tool, I will refer to it as iodump.

<a name="Advantage"></a>

## 2. Advantage

To troubleshoot disk I/O problems of the operating system, run the iostat extended command. If the iostat tool shows that the disk I/O concurrency is high and the disk usage is close to saturation, you need to use more tools to view the process information and read/write file information that affects the high DISK I/O usage. Common tools or methods include iotop, blkTrace, ftrace, and block_dump, but in practice, they all have their limitations. 

1. The iotop tool can be used to refine process bandwidth information, but lacks process-level IOPS information and disk partition information.

2. Blktrace tool, powerful, but more complex to use. After obtaining sector information, it is inefficient to use other methods such as the debugfs to resolve file paths.

3. The ftrace tool is similar to the blktrace tool when tracing static probe points on the block device layer. It also needs to further parse file paths using tools such as debugfs. When tracing probe point functions at the file system layer, the number of IOPS cannot be accurately mapped.

4. Block_dump also has the two disadvantages of ftrace.

Compared with the disk I/O tools, the iodump has the following advantages:

1. Supports custom selection of blk layer probepoint functions.

2. You can customize output fields, including time, process name, process ID, I/O size, sector address, disk partition, read/write type, extended I/O type, I/O source, Inode number, and file full path.

3. When the collection process exits abnormally, the probe can be automatically closed in kernel mode.

4. Support for all kernel versions from 2.6.32 and up.

5. Sampling output is supported when IOPS is high.

The iodump function is powerful, but the iodump system can load kernel modules, which may cause the operating system crash. Test the iodump system before using it in important production environments.

<a name="Installation"></a>

## 3. Installation

To use, there are several methods:

<a name="AnolisOS-CentOS1"></a>

### 3.1. AnolisOS CentOS 1

　　Method under AnolisOS and centos.

```bash
$ yum install rpm-build rpmdevtools git
$ rpmdev-setuptree
$ cd ~/
$ git clone https://gitee.com/anolis/iodump.git
$ cp iodump/spec/iodump.spec ~/rpmbuild/SPECS/
$ tar -zcvf ~/rpmbuild/SOURCES/iodump-$(cat iodump/spec/iodump.spec |grep Version |awk '{print $2}').tar.gz iodump
$ rpmbuild -bb ~/rpmbuild/SPECS/iodump.spec
$ cd ~/rpmbuild/RPMS/x86_64/
$ sudo rpm -ivh iodump-$(uname -r)-*.an8.x86_64.rpm
$ sudo rpm -e iodump-$(uname -r)                   # remove package
```

The iodump tools are essentially kernel driver modules. RPM packages generated on one specific kernel version will not work properly on a different kernel version. Here, we recommend using the following RPM query command to distinguish the contents of name, version, release and ARCH of an RPM package. We used three consecutive horizontal lines to separate the different parts, and the results were obvious. 

```bash
$ rpm -qp iodump-4.19.91-24.8.an8.x86_64-1.0.1-1.an8.x86_64.rpm --queryformat="%{name}---%{version}---%{release}---%{arch}\n"   
iodump-4.19.91-24.8.an8.x86_64---1.0.1---1.an8---x86_64
```

<a name="AnolisOS-CentOS2"></a>

### 3.2. AnolisOS CentOS 2

　　In some cases, multiple versions of the kernel are iterated under a single distribution, and this method is suitable for this situation.

　　In the case of Anolis8, there are several smaller distributions of Anolis8.2, 8.4, and 8.6, each of which corresponds to a different kernel version.

> Release&emsp;&emsp;&emsp;&emsp;&emsp;Kernel Version

> 8.2 ANCK 64 bit&emsp;&emsp;4.19.91-25.8.an8.x86_64

> 8.4 ANCK 64 bit&emsp;&emsp;4.19.91-26.an8.x86_64

> 8.6 ANCK 64 bit&emsp;&emsp;4.19.91-26.1.an8.x86_64

　　Newer versions of gcc are usually also updated and are backward compatible with earlier versions. Therefore, we chose the latest version Anolis8.6 as the baler. Install the rpm package of the kernel-devel for all supported kernel versions.

> Release&emsp;&emsp;&emsp;&emsp;&emsp;Kernel-devel

> 8.2 ANCK 64 bit&emsp;&emsp;kernel-devel-4.19.91-25.8.an8.x86_64

> 8.4 ANCK 64 bit&emsp;&emsp;kernel-devel-4.19.91-26.an8.x86_64

> 8.6 ANCK 64 bit&emsp;&emsp;kernel-devel-4.19.91-26.1.an8.x86_64

　　For the relevant version of kernel-devel package, we recommend searching and downloading in Alibaba open source Mirror site.

> https://developer.aliyun.com/packageSearch

　　The specific packaging method is as follows:


```bash
$ rpm -ivh --force kernel-devel-4.19.91-25.8.an8.x86_64.rpm kernel-devel-4.19.91-26.an8.x86_64.rpm
$ yum install rpm-build rpmdevtools git
$ rpmdev-setuptree
$ cd ~/
$ git clone https://gitee.com/anolis/iodump.git
$ cp iodump/spec/distribution.spec ~/rpmbuild/SPECS/
$ tar -zcvf ~/rpmbuild/SOURCES/iodump-$(cat iodump/spec/iodump.spec |grep Version |awk '{print $2}').tar.gz iodump
$ rpmbuild -bb ~/rpmbuild/SPECS/distribution.spec
$ cd ~/rpmbuild/RPMS/x86_64/
$ sudo rpm -ivh iodump-*.an8.x86_64.rpm
$ sudo rpm -e iodump                                           # remove package
```

<a name="Ubuntu"></a>

### 3.3. Ubuntu

Method under Ubuntu.

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

<a name="Source-Code"></a>

### 3.4. Source Code

Source code installation method.

```bash
$ cd ~/
$ git clone https://gitee.com/anolis/iodump.git
$ cd iodump
$ make 
$ sudo make install
$ sudo make uninstall                                          # remove
```

<a name="instructions"></a>

## 4. instructions

<a name="basic-usage"></a>

### 4.1. basic usage

The basic destiny command is as follows. To emphasize this, the operation requires lifting weights using sudo.

```bash
$ sudo iodump -p sda
```

End Running the iodump, you can use the combination of Ctrl + c or run the following commands on another terminal.

```bash
$ sudo killall iodump
```

<a name="option-usage"></a>

### 4.2. option usage

For a complete understanding of how to use it, we can check out the help information.

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
  -S <number>    Set sample number, only 1/number output is displayed, default 1.
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

The following describes the meanings of major parameters

* -p option: Set the disk or disk partition to track, for example, -p sda or -p sda5. This parameter is mandatory. An error occurs if the program is not set.

* -t option: Set the duration of the tracking program, for example, -t 60 or -t -1, where the program automatically terminates after 60 seconds. -1 means forever. If the -t parameter is not specified, the program ends in 300 seconds by default.

* -s option: Set the file where trace information is stored, for example -s /tmp/log. If the -s parameter is not specified, trace information is printed to the screen standard output, or it can be redirected to save trace information to a disk file.

* -S option: Sets the proportion of sample output. If the iodump output is -s 30, the iodump output will run once every 30 times.

* -H option: Example To mask the output of the title bar information, for example, -h.

* -a option: Set the kernel static probe point to track iodump. The options can be B, C, D, F, G, I, M, Q, R, S, or X. The default value is G, that is, block_getrq, and UNIT is request. Some other probe points UNIT is bio.

* -o option: Example Set the output information field, for example, -o pid,comm. The value can be datetime, timestamp, comm, pid, tid, iosize, sector, partition, rw, rwsec, launcher, ino, or fullpath. Separate the values with commas (,).

* -O option: Set additional output fields, such as -O tid,ino. Default output information fields combination for datetime, comm, pid, iosize, rw, rwsec, launcher, fullpath.

* -c option: Just output exact match comm string record, for example, -c kworker/1:0.

* -C option: Just output record which comm contain the comm string, for example, -C kworker.

* -P option: Just output exact match pid record, for example, -P 1234.

Here are some examples of various parameter combinations running.

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

<a name="output-format"></a>

## 5. output format

Each line of iodump output represents the details of an IO request structure. If the iodump command output is displayed, see the following information.

```bash
$ iodump -p sda
datetime                   comm                pid iosize partition rw rwsec fullpath
2022-04-26T22:53:18.272487 kworker/u128:0    19607   4096 sda3       W     V /var/log/messages
2022-04-26T22:53:18.272503 jbd2/sda3-8         834   8192 sda3       W     S -
2022-04-26T22:53:18.272563 jbd2/sda3-8         834   4096 sda3       W    FS -
2022-04-26T22:53:23.392466 kworker/u128:2    19494   4096 sda3       W     M -
datetime                   comm                pid iosize partition rw rwsec fullpath
```

The output now has nine columns of data items, each of which is explained below.

* datetime： The time format is in microseconds after the decimal point.

* timestamp：Timestamp information in microseconds (us).

* comm：     The name of the process in context.

* pid：      The process ID in the context.

* tid：      ID of the thread in the context.

* iosize：   Size of the I/O data sent to the disk at a time, for example, 4096 or 524288, in bytes. The value must be a multiple of 4096, which is the size of a page.

* sector：   The sector address of a request sent to disk. A sector address is the sector id of a low-level disk formatted before delivery. A sector address is unique on a disk.

* partition：Information about the disk partition where the data of a request sent to a disk resides, for example, sda5, sda, nvme0n1p3, and nvme0n1.

* rw：       IO basic type. R is READ, W is WRITE, D is DISCARD, E is SECURE_ERASE and DISCARD, F is FLUSH, and N is Other.

* rwsec：    IO extension type: F is FUA(Forced Unit Access), A is RAHEAD(Read Ahead), S is SYNC, M is META, and E is SECURE. Multiple options, if none of the above, display V.

* launcher： For the IO call stack originating from user mode, the system call name information is displayed here.

* ino：      The inode number of a request sent to a disk is in the disk partition. Some requests that operate on metadata request this inode information to be 0.

* fullpath： If the inode information is not 0, name of the file and disk path. This resolves the full file path for the disk partition. When disk IOPS are full, you can quickly locate faults by analyzing the FilePath field.

<a name="LICENSE"></a>

## 6. LICENSE

&emsp;&emsp;This project is licensed under the MIT License - see the [LICENSE](./LICENSE) file for details, unless explicitly stated otherwise. Some files in the 'kernel' directory are dual licensed under either GPL v2 or MIT. Relevant license is reminded at the top of each source file.

<a name="get-support"></a>

## 7. get support

The IOPS dump tool is still being developed and optimized. If you have any questions, suggestions on the tool's functions, or want to contribute code, please join us and give feedback. Group number: 31987277 or 33304007.
