include $(BASE)/common.mk

OBJS := interface.o target.o binary-file.o dfu.o target/stm32-usart.o \
target/stk500.o target/dfu-cmd.o target/avrisp.o target/nordic-spi.o \
file-container.o crc32.o jsmn.o

CFLAGS += -DJSMN_PARENT_LINKS

# Keep binary-format-bin last: it is special (last resort)
FORMAT_OBJS := $(subst binary-format-bin.o,,$(patsubst %.c,%.o,$(wildcard binary-format-*.c))) binary-format-bin.o
FORMAT_OBJ := binary-formats.o

RX_METHOD_OBJS := $(patsubst %.c,%.o,$(wildcard rx-method-*.c))
RX_METHOD_OBJ := rx-methods.o

HTTP_URLS_OBJS := $(patsubst %.c,%.o,$(wildcard http-url-*.c))
HTTP_URLS_OBJ := http-urls.o

HTMLS := $(patsubst %.html, %.html.o, $(wildcard http-files/*.html))

STK500_DEVICES_OBJS := $(patsubst %.c, %.o, $(wildcard target/stk500-device-*.c))

STM32_DEVICES_OBJS := $(patsubst %.c, %.o, $(wildcard target/stm32-device-*.c))

ifeq ($(DYNAMIC_LIB),y)
CFLAGS += -fpic -fPIC
EXT := .so
else
EXT := .a
endif

ifeq ($(HOST),linux)
OBJS += host/linux.o interface/linux-serial.o interface/linux-serial-stm32.o \
interface/linux-serial-arduino-uno.o target/dummy-linux.o \
file-container-posix.o interface/linux-spi-bus-pirate.o \
interface/linux-spi-bus-pirate-nordic.o
endif

ifeq ($(HOST),esp8266)
OBJS += host/esp8266.o interface/esp8266-serial.o \
interface/esp8266-serial-star8.o \
interface/esp8266-serial-arduinouno-hacked.o \
interface/esp8266-spi.o \
interface/esp8266-spi-arduinouno-hacked.o
endif

ifeq ($(HAVE_LWIP),y)
CFLAGS += -DHAVE_LWIP
OBJS += picohttpparser.o $(HTTP_URLS_OBJ) $(HTMLS) tcp-conn-lwip-raw.o
endif

ifeq ($(HAVE_SPIFFS),y)
OBJS += spiffs/spiffs_cache.o spiffs/spiffs_check.o spiffs/spiffs_gc.o \
spiffs/spiffs_hydrogen.o spiffs/spiffs_nucleus.o

CFLAGS += -I$(BASE)/src/spiffs -DHAVE_SPIFFS=1
endif

OBJS += $(FORMAT_OBJ) $(RX_METHOD_OBJ) $(STK500_DEVICES_OBJS) \
$(STM32_DEVICES_OBJS)

LIB := libdfu$(EXT)

all: $(LIB)

$(eval $(call install_cmds,$(LIB),$(EXECUTABLES),$(SCRIPTS)))

clean:
	find . -name \*.o -or -name \*~ | xargs rm -f
	rm -f $(LIB) $(EXECUTABLES)
	rm -f url-tab.c


libdfu.a: $(OBJS)
	$(AR) cru $@ $^

libdfu.so: $(OBJS)
	$(CC) -o $@ -shared -Wl,-soname,$@ $^

$(HTMLS): %.html.o: %.html
	$(OBJCOPY) -B $(BINARY_ARCHITECTURE) -I binary -O $(OBJ_FORMAT) $< $@

url-tab.c: $(HTMLS)
	./gen_url_tab $@ $+


$(FORMAT_OBJ): $(FORMAT_OBJS)
	$(LD) -nostartfiles -nodefaultlibs -r -T formats.ld $+ -o $@

$(HTTP_URLS_OBJ): $(HTTP_URLS_OBJS) url-tab.o
	$(LD) -nostartfiles -nodefaultlibs -r -T http-urls.ld $+ -o $@

$(RX_METHOD_OBJ): $(RX_METHOD_OBJS)
	$(LD) -nostartfiles -nodefaultlibs -r -T rx-methods.ld $+ -o $@


.PHONY: install all clean
