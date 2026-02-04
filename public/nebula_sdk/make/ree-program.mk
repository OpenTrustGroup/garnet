# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Make a executable program.

SRC_C_OBJ=$(patsubst %,out/%.o,${SRC_C})

# This target must be first one.
all: ${SRC_C_OBJ}
	@$(CPP) $(LDFLAGS) ${SRC_C_OBJ} ${EXT_LIB} -o out/${TARGET}.unstrip
	@$(STRIP) out/${TARGET}.unstrip out/${TARGET}

clean:
	rm -rf out

run:
	${SDK_HOME}/run/run-ca-in-qemu.sh -e out/${TARGET} -a "${RUN_ARG}"

run-device:
	@echo "This is a ree program."

run-qemu:
	@echo "This is a ree program."
