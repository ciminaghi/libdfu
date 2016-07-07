
include $(BASE)/common.mk

ifeq ($(HOST),linux)
EXE := linux-stm32
endif

all: $(EXE)

$(eval $(call install_cmds,$(LIB),$(EXE),$(SCRIPTS)))

clean:
	rm -f $(EXE) *.o *~

all: $(EXE)
