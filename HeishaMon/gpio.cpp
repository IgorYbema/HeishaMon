#include "gpio.h"
#include "src/common/progmem.h"
#include "src/common/stricmp.h"
const char* mqtt_topic_gpio PROGMEM = "gpio";

void log_message(char* string);

void setupGPIO(gpioSettingsStruct gpioSettings) {
  for (int i = 0 ;  i < NUMGPIO ; i++) {
    pinMode(gpioSettings.gpioPin[i], gpioSettings.gpioMode[i]);
  }
}

void mqttGPIOCallback(char* topic, char* value) {
  char log_msg[256];
  sprintf_P(log_msg, PSTR("GPIO: MQTT message received on subtopic '%s' value '%s'"), topic, value);
  log_message(log_msg);

    // --- diagnostic: dump exact topic bytes/length before the strcmp ---
    size_t tlen = strlen(topic);
    int n = sprintf_P(log_msg, PSTR("GPIO: topic len=%u bytes:"), (unsigned)tlen);
    for (size_t i = 0; i < tlen && n < (int)sizeof(log_msg) - 4; i++) {
      n += sprintf(log_msg + n, " %02X", (unsigned char)topic[i]);
    }
    log_message(log_msg);
    // -------------------------------------------------------------------
  
#ifdef ESP32
  log_message(_F("GPIO: Does it even go here?'"));
  if (strcmp_P(topic, PSTR("relay/one")) == 0) {
    log_message(_F("GPIO: MQTT message received 'relay/one'"));
    digitalWrite(relayOnePin,((stricmp((char*)"true", value) == 0) || (stricmp((char*)"on", value) == 0)  || (stricmp((char*)"enable", value) == 0)|| (String(value).toInt() == 1 )));
  } else if (strcmp_P(topic,PSTR("relay/two")) == 0) {
    log_message(_F("GPIO: MQTT message received 'relay/two'"));
    digitalWrite(relayTwoPin,((stricmp((char*)"true", value) == 0) || (stricmp((char*)"on", value) == 0)  || (stricmp((char*)"enable", value) == 0)|| (String(value).toInt() == 1 )));
  }
#endif
}
