# This program is used for "cache side attack".
# Use Flush-Reload method.
# step:
# 1. Start "app" as a service in TEE.
# 2. Start "hacker_ta/ca" as app to allocate a shared memory with service.
# 3. Hacker flushes shared memory, and then send event to service.
# 4. Service writes some data into shared memory.
# 5. Hacker will judge which memory is used by service, by time of reloading data from memory.
# run:
# 1. run-tata : run app as service, and then hacker_ta to attack shared memory with app.
#    In actual, it has effect.
# 2. run-cata : run app as service, and then hacker_ca to attack shared memory with app.
#   In actual, it has NO effect.

sub= app hacker_ta hacker_ca

sub_clean=$(patsubst %,%_clean,${sub})
# - "make run" for all execept android, because it needs android.
sub_run=$(patsubst %,%_run, $(filter-out android,${sub}))

.PHONY:all ${sub} ${sub_clean} ${sub_run}

# It's should be the first target, and then it's the default target.
all: ${sub}

${sub}:
	make -j -C $@

${sub_clean}:
	make -C $(patsubst %_clean,%,$@) clean

clean: ${sub_clean}

# Just test program run.
run-qemu:
	make -C app run-qemu
	make -C hacker_ta run-qemu

run-tata:
	make -C app run-device
	make -C hacker_ta run-device

run-cata:
	make -C app run-device
	make -C hacker_ca run-ndk
