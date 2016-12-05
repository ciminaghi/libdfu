
-include local_config.mk

BASE := $(shell pwd)

HOST ?= esp8266

SUBDIRS:=src samples

output_tar_name ?= $(shell echo libdfu-`date +%Y%m%d`.tar.bz2)
arduino_output_zip_name ?= \
	$(BASE)/$(shell echo -n libdfu-$$(date +%Y%m%d)-g$$(git rev-parse --verify --short HEAD).zip)

export BASE HOST DYNAMIC_LIB SAMPLES_SSID SAMPLES_PASSWORD SDK_BASE

all: subdirs

install: all subdirs_install

clean: subdirs_clean

subdirs: $(SUBDIRS)

subdirs_clean subdirs_install: subdirs_%: $(foreach s,$(SUBDIRS),$(s)_%)

arduino_zip:
	./arduino/build_src_zip $(arduino_output_zip_name)

$(SUBDIRS):
	make -C $@

$(foreach s,$(SUBDIRS),$(s)_clean): %_clean:
	make -C $* clean

$(foreach s,$(SUBDIRS),$(s)_install): %_install:
	make -C $* install

tar : clean
	@echo "BUILDING TAR $(output_tar_name)" && cd .. && \
        [ ! -f $(output_tar_name) ] && \
        tar jcf $(output_tar_name) `basename $(BASE)` \
        || { echo "!!! TAR IS ALREADY THERE " ; exit 1 ; }

tar_compact : clean
	@echo "BUILDING COMPACT TAR (no git) $(output_tar_name)" && cd .. && \
        [ ! -f $(output_tar_name) ] && \
        tar jcf $(output_tar_name) --exclude '.git' `basename $(BASE)` \
        || { echo "!!! TAR IS ALREADY THERE " ; exit 1 ; }


.PHONY: all subdirs $(SUBDIRS) tar clean $(foreach s,$(SUBDIRS),$(s)_clean) \
subdirs_all subdirs_clean subdirs_install
