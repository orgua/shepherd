TARGET_MODULE  = shepherd
EXTRA_DIR = /lib/modules/$(shell uname -r)/extra/

# If we are running by kernel building system
ifneq ($(KERNELRELEASE),)
    obj-m += $(TARGET_MODULE).o
    $(TARGET_MODULE)-objs = module_base.o
    $(TARGET_MODULE)-objs += pru_firmware.o
    $(TARGET_MODULE)-objs += pru_mem_interface.o
    $(TARGET_MODULE)-objs += pru_msg_sys.o
    $(TARGET_MODULE)-objs += ocmc_cache.o
    $(TARGET_MODULE)-objs += pru_sync_control.o
    $(TARGET_MODULE)-objs += sysfs_interface.o
else
	BUILDSYSTEM_DIR:=/lib/modules/$(shell uname -r)/build
	PWD:=$(shell pwd)
endif

ccflags-y=-I$(src)/../common

#CFLAGS_MODULE = -Wall -Wextra
# -> not enabled by default because it also includes warnings of all external files
# Analyze with:
# make 2> output.txt
# -> look for "shepherd/software/kernel" but
# 	 you can ignore lines with: (in file included) from .. shepherd/
# /opt/shepherd/software/kernel-module/src/ is at beginning of line

all: build

build:
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) clean

load: # temporary during runtime
	insmod ./$(TARGET_MODULE).ko

unload:
	rmmod ./$(TARGET_MODULE).ko

install: build
	@echo 'Installing kernel module'
	-@sudo mkdir -p $(EXTRA_DIR)
	@sudo cp $(TARGET_MODULE).ko $(EXTRA_DIR)
# also add name "shepherd" to /etc/modules for autostart
