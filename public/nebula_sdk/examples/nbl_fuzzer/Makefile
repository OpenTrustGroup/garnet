# Makefile all projects in samples.

# list sub directories for building.
sub= simple

sub_clean=$(patsubst %,%_clean,${sub})
sub_run=$(patsubst %,%_run,${sub})
sub_run_qemu=$(patsubst %,%_run-qemu,${sub})
sub_run_device=$(patsubst %,%_run-device,${sub})

.PHONY:all ${sub} ${sub_clean} ${sub_run}

# It's should be the first target, and then it's the default target.
all: ${sub}

${sub}:
	make -C $@

${sub_clean}:
	make -C $(patsubst %_clean,%,$@) clean

clean: ${sub_clean}

format:
	make -C simple format

${sub_run}:
	make -C $(patsubst %_run,%,$@) run

${sub_run_device}:
	make -C $(patsubst %_run-device,%,$@) run-device

${sub_run_qemu}:
	make -C $(patsubst %_run-qemu,%,$@) run-qemu

run: ${sub_run}

run-qemu: ${sub_run_qemu}

run-device: ${sub_run_device}
