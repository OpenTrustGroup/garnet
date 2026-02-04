# Example for making shared library.

sub= lib exe

sub_clean=$(patsubst %,%_clean,${sub})
sub_run=$(patsubst %,%_run,${sub})

.PHONY:all ${sub} ${sub_clean} ${sub_run}

# It's should be the first target, and then it's the default target.
all: ${sub}

exe : lib

${sub}:
	make -C $@

${sub_clean}:
	make -C $(patsubst %_clean,%,$@) clean

clean: ${sub_clean}

run:
	@echo "Run nothing, because this example needs special method to run."
	@echo "In this folder, input below commands:"
	@echo "1, [console 1][In this folder] make install"
	@echo "2, [console 2][In SDK folder ] run/run-qemu.sh"
	@echo "3, [console 1][In this folder] make test"

# install the .so into the system image.
install:
	make -C lib install

# install the HA and run it.
test:
	make -C exe run

run-qemu:
	@echo "Run nothing, because this example needs special method to run."
	@echo "In this folder, input below commands:"
	@echo "1, [console 1][In this folder] make install"
	@echo "2, [console 2][In SDK folder ] run/run-qemu.sh"
	@echo "3, [console 1][In this folder] make test"
run-device:
	@echo "Run nothing, because this example needs special method to run."
	@echo "In this folder, input below commands:"
	@echo "1, [console 1][In this folder] make install"
	@echo "2, [console 2][In SDK folder ] run/run-qemu.sh"
	@echo "3, [console 1][In this folder] make test"
