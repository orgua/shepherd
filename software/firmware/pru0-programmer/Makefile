PRU_NUM = 0
mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
current_dir := $(notdir $(patsubst %/,%,$(dir $(mkfile_path))))
folder_path := $(dir $(mkfile_path))
PROTO ?= SWD
PROJ_NAME = $(current_dir)-$(PROTO)
FW_NAME = programmer-$(PROTO)
# choose variant with "make PROTO=SWD|SBW", with SWD being default

BASE_PATH ?= ./..

# include headers with -I or --include_path= (cgt-only)
INCLUDE = -I./include
# small hack to get resource_table without code-duplication
INCLUDE += -I$(BASE_PATH)/pru0-shepherd-fw/include


# external sources - add manually
SOURCES = $(BASE_PATH)/lib/src/simple_lock.c
SOURCES += $(BASE_PATH)/lib/src/iep.c
SOURCES += $(BASE_PATH)/lib/src/ringbuffer.c
# internal sources
SOURCES += $(notdir $(wildcard ./*.c))

# internal asm-sources
ASM_SOURCES = $(notdir $(wildcard ./*.asm))
# external asm-sources - add manually
#ASM_SOURCES += spi_transfer_pru.asm # adc_readwrite, adc_fastread, dac_write

ifeq ($(PROTO), SBW)  # switch to SBW with "make PROTO=SBW"
CFLAGS += -D SBW_SUPPORT
endif

# look for gcc, but use cgt as fallback / default
ifdef PRU_GCC
include $(BASE_PATH)/lib/GCC/Makefile
else
include $(BASE_PATH)/lib/CGT/Makefile
endif
