# Samples for make a far running in HEE.

SDK_HOME ?= ../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += \
  -I${SDK_HEE_HOME}/trusty/include \
  -I${SDK_HEE_HOME}/nbl_gui/include \
  -I${SDK_HEE_HOME}/nbl_fwk_hee/include \

LDFLAGS += -L${SDK_HEE_HOME}/nbl_gui/lib/ -lnbl_gui

SRC_C = main.cc manifest.c

ifeq ($(SD_ALLOW), 0)
  SD_PRIVILEGE_FILE = sd.privilege.forbid
else ifeq ($(SD_ALLOW), 1)
  SD_PRIVILEGE_FILE = sd.privilege.allow
endif

# Program name in "out" folder.
TARGET = tui_privilege_forbid

FAR_FILES += \
  assets/layout.json=layout.json, \
  assets/layout.css=layout.css, \

include ${SDK_HOME}/make/hee-tui.mk
