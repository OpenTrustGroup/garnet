# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Make a shared library for Linux.

SRC_C_OBJ=$(patsubst %,out/%.o,${SRC_C})

# This target must be first one.
LIB_NAME=lib${TARGET}.so
CFLAGS += -fPIC

all: ${SRC_C_OBJ}
	@$(CPP) $(LDFLAGS) -shared ${SRC_C_OBJ} ${EXT_LIB} -o out/${LIB_NAME}.unstrip
	@$(STRIP) out/${LIB_NAME}.unstrip out/${LIB_NAME}

clean:
	@rm -rf out

run:
	@echo "This is a shared library."

run-device:
	@echo "This is a shared library."

run-qemu:
	@echo "This is a shared library."
