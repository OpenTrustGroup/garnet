# Make for client and server

# make && make run-qemu && make cov or make lcov
# You can see result in "lib/out".

# list sub directories for building.
# lib : static library which should be calculated coverage.
# test: test lib with googletest.
sub= lib test

sub_clean=$(patsubst %,%_clean,${sub})
sub_run=$(patsubst %,%_run,${sub})
sub_run_qemu=$(patsubst %,%_run-qemu,${sub})
sub_run_device=$(patsubst %,%_run-device,${sub})

sub_cov=$(patsubst %,%_cov,${sub})
sub_lcov=$(patsubst %,%_lcov,${sub})

.PHONY:all ${sub} ${sub_clean} ${sub_run} ${sub_cov} ${sub_lcov} cov lcov 

# It's should be the first target, and then it's the default target.
all: ${sub}

${sub}:
	make -C $@

${sub_clean}:
	make -C $(patsubst %_clean,%,$@) clean

clean: ${sub_clean}

${sub_run}:
	make -C $(patsubst %_run,%,$@) run

${sub_run_device}:
	make -C $(patsubst %_run-device,%,$@) run-device

${sub_run_qemu}:
	make -C $(patsubst %_run-qemu,%,$@) run-qemu

run: ${sub_run}

run-qemu: ${sub_run_qemu}

run-device: ${sub_run_device}

${sub_cov}:
	make -C $(patsubst %_cov,%,$@) cov


#### Run by test Makefile, but calculate coverage about static library in "lib" Makefile.

cov:
	make -C lib cov

lcov:
	make -C lib lcov

########################

test: lib
