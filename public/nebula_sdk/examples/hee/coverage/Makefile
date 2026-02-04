# Make for coverage of clang.

# YOU should install "lcov".
# sudo apt install lcov

# If you want to test one coverage exe,
# You can "make" "make run-qemu", and then "make cov" or "make lcov".

# list sub directories for building.
# test_tar: test libtar.
# test_files: want get coverage about multiple files and subdir in source.
# test_cpp: test cpp files.
# test_static_lib: want to get coverage of static library, and use googletest.
# test_cs: want to get coverage data of client and server.
sub= test_tar test_files test_cpp test_cs test_static_lib

sub_clean=$(patsubst %,%_clean,${sub})
sub_run=$(patsubst %,%_run,${sub})
sub_run_qemu=$(patsubst %,%_run-qemu,${sub})
sub_run_device=$(patsubst %,%_run-device,${sub})

.PHONY:all ${sub} ${sub_clean} ${sub_run} tar test_tar cov lcov 

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

########################

test_tar_test: test_tar
	make -C test_tar run-qemu

test_files_cov: test_files
	make -C test_files cov

test_files_lcov: test_files
	make -C test_files lcov
