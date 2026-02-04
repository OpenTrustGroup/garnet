# Copyright 2018 GoldenRiver Technology Co., Ltd. All rights reserved.

# There are 2 projects.
# - main is used to control the test process.
# - tester is used to do the real test.

sub= main tester

sub_clean=$(patsubst %,%_clean,${sub})
sub_format=$(patsubst %,%_format,${sub})

.PHONY:all ${sub} install-qemu install-device format

# It's should be the first target, and then it's the default target.
all: ${sub}

${sub}:
	make -f $@/Makefile all

${sub_clean}:
	make -C $(patsubst %_clean,%,$@) clean

clean:
	rm -rf ./out

${sub_format}:
	make -f $(patsubst %_format,%,$@)/Makefile format

format: ${sub_format}

install-qemu:
	make -f main/Makefile install-qemu
	make -f tester/Makefile install-qemu

install-device:
	make -f main/Makefile install-device
	make -f tester/Makefile install-device
