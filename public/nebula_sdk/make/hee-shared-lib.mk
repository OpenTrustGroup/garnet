# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Make a shared library for HEE.
# - TODO: Currently the dynamic library can not be loaded into HEE by loader.

LOCAL_OBJS_DIR ?= out

SRC_C_OBJ=$(patsubst %,${LOCAL_OBJS_DIR}/%.o,${SRC_C})

# This target must be first one.
# - clang use ${CC} as ld.

ifeq (${enable_deprecated_syscalls}, true)
  CFLAGS += -DENABLE_TRUSTY_DEPRECATED_SYSCALL
endif

LIB_NAME=lib${TARGET}.so
CFLAGS += -fPIC

all: ${SRC_C_OBJ}
	$(NOECHO)$(CPP) $(LDFLAGS) -shared ${SRC_C_OBJ} ${EXT_LIB} -o ${LOCAL_OBJS_DIR}/${LIB_NAME}.unstrip
	$(NOECHO)$(STRIP) ${LOCAL_OBJS_DIR}/${LIB_NAME}.unstrip ${LOCAL_OBJS_DIR}/${LIB_NAME}

clean:
	rm -rf ${LOCAL_OBJS_DIR}

run:
	@echo "This is a shared library."

run-device:
	@echo "This is a shared library."

run-qemu:
	@echo "This is a shared library."
