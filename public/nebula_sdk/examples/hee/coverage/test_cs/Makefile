# Make for client and server

# make && make run-qemu && make cov or make lcov.
# You can see result in service/out and client/out.

# list sub directories for building.
# service : create a service for client connecting.
# client: connect to service and request some function.
sub= service client

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

cov: ${sub_cov}

${sub_lcov}:
	make -C $(patsubst %_lcov,%,$@) lcov

lcov: ${sub_lcov}

########################

client: service
