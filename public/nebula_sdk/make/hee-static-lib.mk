# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Make a static library for HEE.

LOCAL_OBJS_DIR ?= out

SRC_C_OBJ=$(patsubst %,${LOCAL_OBJS_DIR}/%.o,${SRC_C})

ifeq (${enable_deprecated_syscalls}, true)
  CFLAGS += -DENABLE_TRUSTY_DEPRECATED_SYSCALL
endif

# This target must be first one.
# - clang use ${CC} as ld.
all: ${SRC_C_OBJ}
	@$(AR) rcsD ${LOCAL_OBJS_DIR}/lib${TARGET}.a ${SRC_C_OBJ}

clean:
	rm -rf ${LOCAL_OBJS_DIR}

run:
	@echo "This is a static library."

run-device:
	@echo "This is a shared library."

run-qemu:
	@echo "This is a shared library."
