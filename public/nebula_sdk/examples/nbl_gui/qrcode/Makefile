# TUI exampley: QRCode
# Show a QRCode created by a random string in window.

SDK_HOME ?= ../../..

# include common setting of makefile.
include ${SDK_HOME}/make/hee-common.mk

CFLAGS += \
  -I${SDK_HEE_HOME}/trusty/include \
  -I${SDK_HEE_HOME}/nbl_gui/include \
  -I${SDK_HEE_HOME}/nbl_fwk_hee/include \
  -I${SDK_HEE_HOME}/nbl_qrcode/include \

LDFLAGS += -L${SDK_HEE_HOME}/nbl_qrcode/lib/ -lnbl_qrcode

EXT_LIB += ${SDK_HEE_HOME}/nbl_gui/lib/libnbl_gui.so

SRC_C = main.cc manifest.c

SD_PRIVILEGE_FILE = sd.privilege

# Program name in "out" folder.
TARGET = tui_qrcode

FAR_FILES += \
  data/box.png=data/box.png, \
  data/gr1.png=data/gr1.png, \
  data/back.png=data/back.png, \
  data/back2.png=data/back2.png, \
  data/layout.json=data/layout.json, \
  data/layout.css=data/layout.css, \
  data/DroidSansFallbackFull.ttf=data/DroidSansFallbackFull.ttf, \

include ${SDK_HOME}/make/hee-tui.mk
