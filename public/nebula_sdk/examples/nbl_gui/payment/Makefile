# TUI Example: Pay !
# Input a password of payment.

SDK_HOME ?= ../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += \
  -I${SDK_HEE_HOME}/trusty/include \
  -I${SDK_HEE_HOME}/nbl_gui/include \
  -I${SDK_HEE_HOME}/nbl_fwk_hee/include \

LDFLAGS +=

EXT_LIB += ${SDK_HEE_HOME}/nbl_gui/lib/libnbl_gui.so

SRC_C = main.cc manifest.c

SD_PRIVILEGE_FILE = sd.privilege

# Program name in "out" folder.
TARGET = tui_payment

FAR_FILES += \
  data/gr1.png=data/gr1.png, \
  data/back.png=data/back.png, \
  data/back2.png=data/back2.png, \
  data/layout.json=data/layout.json, \
  data/layout.css=data/layout.css, \
  data/DroidSansFallbackFull.ttf=data/DroidSansFallbackFull.ttf, \

include ${SDK_HOME}/make/hee-tui.mk
