# Copyright 2019 GoldenRiver Technology Co., Ltd. All rights reserved.

# TUI's examples.

SUBDIRS := $(wildcard */.)

define submake
    for d in $(SUBDIRS);                       \
    do                                         \
        $(MAKE) $(1) --directory=$$d || exit1; \
    done
endef

all:
	$(call submake,$@)

clean:
	$(call submake,$@)

.PHONY: all clean $(SUBDIRS)
