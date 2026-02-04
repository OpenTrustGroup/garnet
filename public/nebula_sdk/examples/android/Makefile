# Makefile all projects in samples.

# list sub directories for building.
sub= trusty

sub_clean=$(patsubst %,%_clean,${sub})
sub_run=$(patsubst %,%_run,${sub})

.PHONY:all ${sub} ${sub_clean} ${sub_run}

# It's should be the first target, and then it's the default target.
all: ${sub}

${sub}:
	make -C $@

${sub_clean}:
	make -C $(patsubst %_clean,%,$@) clean

clean: ${sub_clean}

${sub_run}:
	make -C $(patsubst %_run,%,$@) run

run: ${sub_run}
