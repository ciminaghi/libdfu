#ifndef __DFU_HOST_H__
#define __DFU_HOST_H__

#ifdef HOST_esp8266
#include "dfu-esp8266.h"
#elif defined HOST_linux
#include "dfu-linux.h"
#else
#error "HOST is not defined"
#endif

#endif /* __DFU_HOST_H__ */

