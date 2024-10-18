PRU_NUM = 1
mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
current_dir := $(notdir $(patsubst %/,%,$(dir $(mkfile_path))))
folder_path := $(dir $(mkfile_path))
PROJ_NAME = $(current_dir)
FW_NAME = shepherd

BASE_PATH ?= ./..

# include headers with -I or --include_path= (cgt-only)
INCLUDE = -I./include

# external sources - add manually
SOURCES = $(BASE_PATH)/lib/src/iep.c
SOURCES += $(BASE_PATH)/lib/src/msg_sys.c
# internal sources
SOURCES += $(notdir $(wildcard ./*.c))

# internal asm-sources
ASM_SOURCES = $(notdir $(wildcard ./*.asm))
# external asm-sources - add manually
#ASM_SOURCES += $(BASE_PATH)/lib/src/spi_transfer_pru.asm # adc_readwrite, adc_fastread, dac_write


# look for gcc, but use cgt as fallback / default
ifdef PRU_GCC
include $(BASE_PATH)/lib/GCC/Makefile
else
include $(BASE_PATH)/lib/CGT/Makefile
endif
