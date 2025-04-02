PRU_NUM = 0
mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
current_dir := $(notdir $(patsubst %/,%,$(dir $(mkfile_path))))
folder_path := $(dir $(mkfile_path))
TYPE ?= EMU
PROJ_NAME = $(current_dir)-$(TYPE)
FW_NAME = shepherd-$(TYPE)
# choose variant with "make TYPE=EMU|HRV", with EMU being default

BASE_PATH ?= ./..

# include headers with -I or --include_path= (cgt-only)
INCLUDE = -I./include

# external sources - add manually
SOURCES += $(BASE_PATH)/lib/src/iep.c
SOURCES += $(BASE_PATH)/lib/src/msg_sys.c
# internal sources
SOURCES += $(notdir $(wildcard ./*.c))

# internal asm-sources
ASM_SOURCES = $(notdir $(wildcard ./*.asm))
# external asm-sources - add manually
# ... currently empty

ifeq ($(TYPE), HRV)  # switch to HRV with "make TYPE=HRV"
CFLAGS += -DHRV_SUPPORT
endif
ifeq ($(TYPE), EMU)
CFLAGS += -DEMU_SUPPORT
endif

# look for gcc, but use cgt as fallback / default
ifdef PRU_GCC
ASM_SOURCES += $(BASE_PATH)/lib/src/spi_transfer_pru_gcc.asm # small hack: constants are defined differently
include $(BASE_PATH)/lib/GCC/Makefile
else
ASM_SOURCES += $(BASE_PATH)/lib/src/spi_transfer_pru.asm # adc_readwrite, adc_fastread, dac_write
ASM_SOURCES += $(BASE_PATH)/lib/src/register_magic.asm # get_size_in_bits, log2safe, max_value, min_value
include $(BASE_PATH)/lib/CGT/Makefile
endif
