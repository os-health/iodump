ifneq ($(KERNELRELEASE),)
obj-m:=kiodump.o
else
KERNELVER?=$(shell uname -r)
ifeq ("$(origin KVER)", "command line")
  KERNELVER:=$(KVER)
  KERNELDIR:=/usr/src/kernels/$(KERNELVER)/
else
  KERNELDIR:=/lib/modules/$(KERNELVER)/build
endif
CURDIR:=$(shell pwd)
DISTRIBUTION_ID:=$(shell [ -f /etc/os-release ] && . /etc/os-release && echo $${ID})
ifeq ($(DISTRIBUTION_ID),)
DISTRIBUTION_ID:=$(shell [ -f /etc/centos-release ] && echo -n "centos")
endif
ifeq ($(DISTRIBUTION_ID),)
DISTRIBUTION:=0
else ifeq ("$(DISTRIBUTION_ID)", "alios")
DISTRIBUTION:=65
else ifeq ("$(DISTRIBUTION_ID)", "anolis")
DISTRIBUTION:=130
else ifeq ("$(DISTRIBUTION_ID)", "centos")
DISTRIBUTION:=67
else ifeq ("$(DISTRIBUTION_ID)", "kylin")
DISTRIBUTION:=75
else ifeq ("$(DISTRIBUTION_ID)", "ubuntu")
DISTRIBUTION:=85
else
DISTRIBUTION:=0
endif
EXTRA_CFLAGS?= -DDISTRIBUTION=${DISTRIBUTION}

default:
	$(MAKE) -C $(KERNELDIR) M=$(CURDIR) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(CURDIR) clean
endif
