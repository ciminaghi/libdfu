
include $(BASE)/common.mk

ifeq ($(HOST),linux)
EXE := linux-stm32 linux-arduino-uno linux-spi-bus-pirate-nordic

ifeq ($(HAVE_LWIP),y)
EXE += linux-http-lwip-stm32
endif

else ifeq ($(HOST),esp8266)
EXE := esp8266-stm32 esp8266-http-lwip-arduinouno esp8266-arduinoprimo-nordic
endif

ifeq ($(HOST),linux)
all: $(EXE)

linux-stm32: % : %.o
	$(CC) -o $@ $+ $(LDFLAGS)

linux-http-lwip-stm32: % : %.o mintapif.o timer.o
	$(CC) -o $@ $+ $(LDFLAGS)
endif

ifeq ($(HOST),esp8266)

all: images

images: $(foreach e,$(EXE),do_$(e)_images)

do_%_images: %
	$(ESPTOOL) elf2image -o $*- $<

$(EXE): % : %.o
	$(LD) -L$(SDK_LIBDIR) $+ $(LD_SCRIPT) $(LDFLAGS) -Wl,--start-group $(LIBS) $(APP_AR) -Wl,--end-group -o $@

endif

$(eval $(call install_cmds,$(LIB),$(EXE),$(SCRIPTS)))

clean:
	rm -f $(EXE) *.o *~ *.bin *.out


.phony: all clean images $(foreach e,$(EXE),do_$(e)_images)
