# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Make a executable program for Linux.

SRC_C_OBJ=$(patsubst %,out/%.o,${SRC_C})

# This target must be first one.
all: ${SRC_C_OBJ}
	@$(AR) rcsD out/lib${TARGET}.a ${SRC_C_OBJ}

clean:
	rm -rf out

run:
	@echo "This is a static library."

run-device:
	@echo "This is a static library."

run-qemu:
	@echo "This is a static library."
