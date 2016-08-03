include $(BASE)/common.mk

OBJS := interface.o target.o binary-file.o dfu.o target/stm32-usart.o

# Keep binary-format-bin last: it is special (last resort)
FORMAT_OBJS := $(subst binary-format-bin.o,,$(patsubst %.c,%.o,$(wildcard binary-format-*.c))) binary-format-bin.o
FORMAT_OBJ := binary-formats.o

RX_METHOD_OBJS := $(patsubst %.c,%.o,$(wildcard rx-method-*.c))
RX_METHOD_OBJ := rx-methods.o

ifeq ($(DYNAMIC_LIB),y)
CFLAGS += -fpic -fPIC
EXT := .so
else
EXT := .a
endif

ifeq ($(HOST),linux)
OBJS += host/linux.o interface/linux-serial.o target/dummy-linux.o
endif

ifeq ($(HOST),esp8266)
OBJS += host/esp8266.o interface/esp8266-serial.o
endif

OBJS += $(FORMAT_OBJ) $(RX_METHOD_OBJ)

LIB := libdfu$(EXT)

all: $(LIB)

$(eval $(call install_cmds,$(LIB),$(EXECUTABLES),$(SCRIPTS)))

clean:
	find . -name \*.o -or -name \*~ | xargs rm -f
	rm -f $(LIB) $(EXECUTABLES)


libdfu.a: $(OBJS)
	$(AR) cru $@ $^

libdfu.so: $(OBJS)
	$(CC) -o $@ -shared -Wl,-soname,$@ $^

$(FORMAT_OBJ): $(FORMAT_OBJS)
	$(LD) -nostartfiles -nodefaultlibs -r -T formats.ld $+ -o $@

$(RX_METHOD_OBJ): $(RX_METHOD_OBJS)
	$(LD) -nostartfiles -nodefaultlibs -r -T rx-methods.ld $+ -o $@


.PHONY: install all clean
