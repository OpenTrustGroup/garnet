# Makefile all projects in samples.

# list sub directories for building.
sub= ree android hee nbl_fwk nbl_gui nbl_attest security_domain

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

${sub_run}:
	make -C $(patsubst %_run,%,$@) run

run: ${sub_run}
ree_run:hee_run
