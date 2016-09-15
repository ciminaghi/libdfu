
include $(BASE)/common.mk

ifeq ($(HOST),linux)
EXE := linux-stm32

ifeq ($(HAVE_LWIP),y)
EXE += linux-http-lwip-stm32
endif

else ifeq ($(HOST),esp8266)
EXE := esp8266-stm32
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

$(EXE).o: $(EXE).c
	$(CC) $(CFLAGS) -c -o $@ $<

$(EXE).out: $(EXE).o
	$(LD) -L$(SDK_LIBDIR) $+ $(LD_SCRIPT) $(LDFLAGS) -Wl,--start-group $(LIBS) $(APP_AR) -Wl,--end-group -o $@


images: $(EXE).out
	$(ESPTOOL) elf2image -o ./ $<

endif

$(eval $(call install_cmds,$(LIB),$(EXE),$(SCRIPTS)))

clean:
	rm -f $(EXE) *.o *~ *.bin *.out


.phony: images all clean
