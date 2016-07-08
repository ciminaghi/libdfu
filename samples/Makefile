
include $(BASE)/common.mk

ifeq ($(HOST),linux)
EXE := linux-stm32
else ifeq ($(HOST),esp8266)
EXE := esp8266-stm32
endif

all: $(EXE)

$(eval $(call install_cmds,$(LIB),$(EXE),$(SCRIPTS)))

clean:
	rm -f $(EXE) *.o *~

all: $(EXE)

ifeq ($(HOST),esp8266)

$(EXE).o: $(EXE).c
	$(CC) $(CFLAGS) -c -o $@ $<

$(EXE): $(EXE).o
	$(LD) -L$(SDK_LIBDIR) $(LD_SCRIPT) $(LDFLAGS) -Wl,--start-group $(LIBS) $(APP_AR) -Wl,--end-group -o $@ $+

endif
