#include <dfu.h>
#include <dfu-host.h>
#include <dfu-cmd.h>
#include <user_config.h>
#include <dfu-internal.h>
#include <stk500-device.h>
#include <dfu-stk500.h>
#include <dfu-linux.h>
#include <esp8266-serial.h>
#include <dfu-esp8266.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

struct dfu_data *global_dfu;
struct dfu_binary_file *global_binary_file;

const char* ssid = "SSID";
const char* password = "PASSWORD";

void setup() {
  Serial1.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
  delay(500);
  WiFi.begin(ssid, password);  

  global_dfu = dfu_init(&esp8266_serial_arduinouno_hacked_interface_ops,
                        NULL,
                        NULL,
                        &stk500_dfu_target_ops,
                        &atmega328p_device_data,
                        &esp8266_dfu_host_ops);

  if (!global_dfu) {
    Serial1.printf("Error initializing dfu library");
    /* FIXME: Is this ok ? */
    return;
  }

  global_binary_file = dfu_binary_file_start_rx(&dfu_rx_method_http_arduino, global_dfu, NULL);
  if (!global_binary_file) {
    Serial1.printf("Error instantiating binary file");
    return;
  }
  
  if (dfu_binary_file_flush_start(global_binary_file) < 0)
      Serial1.printf("Error in dfu_binary_file_flush_start()");
}

void loop() {
   static int done = 0;
  
   switch (dfu_idle(global_dfu)) {
    case DFU_ERROR:
      Serial1.printf("Error programming file");
      break;
    case DFU_ALL_DONE:
      if (done)
          break;
      done = 1;
      Serial1.printf("Programming OK");
      dfu_target_go(global_dfu);
      break;
    case DFU_CONTINUE:
      break;
    }
}
