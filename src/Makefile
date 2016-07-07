include $(BASE)/common.mk

OBJS := interface.o target.o binary-file.o dfu.o target/stm32-usart.o

ifeq ($(DYNAMIC_LIB),y)
CFLAGS += -fpic -fPIC
EXT := .so
else
EXT := .a
endif

ifeq ($(HOST),linux)
OBJS += host/linux.o interface/linux-serial.o
endif

LIB := libdfu$(EXT)

all: $(LIB)

$(eval $(call install_cmds,$(LIB),$(EXECUTABLES),$(SCRIPTS)))

clean:
	rm -f *.o *~ $(LIB) $(EXECUTABLES)


libdfu.a: $(OBJS)
	$(AR) rcs $@ $^

libdfu.so: $(OBJS)
	$(CC) -o $@ -shared -Wl,-soname,$@ $^

.PHONY: install all clean
