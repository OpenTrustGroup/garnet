# Example for making shared library.

sub= lib exe

sub_clean=$(patsubst %,%_clean,${sub})

.PHONY:all ${sub} ${sub_clean} run

# It's should be the first target, and then it's the default target.
all: ${sub}

exe: lib

${sub}:
	make -C $@

${sub_clean}:
	make -C $(patsubst %_clean,%,$@) clean

clean: ${sub_clean}

${sub_run}:
	make -C $(patsubst %_run,%,$@) run

run:
	make -C lib install
	make -C exe run
