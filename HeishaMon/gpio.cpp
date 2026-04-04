#include "gpio.h"
#include <PubSubClient.h>
#include "src/common/progmem.h"
#include "src/common/stricmp.h"
#include "src/common/webserver.h"
const char* mqtt_topic_gpio PROGMEM = "gpio";

void log_message(char* string);

static bool toGPIOValue(char* value) {
  return (stricmp((char*)"true", value) == 0) || (stricmp((char*)"on", value) == 0) ||
         (stricmp((char*)"enable", value) == 0) || (String(value).toInt() == 1);
}

void setupGPIO(gpioSettingsStruct &gpioSettings) {
  // Apply user-configurable modes to the non-relay GPIO pins
  for (int i = 0; i < NUMGPIO_USER; i++) {
    switch (gpioSettings.gpioUserMode[i]) {
      case GPIO_MODE_INPUT:        gpioSettings.gpioMode[i] = INPUT;        break;
      case GPIO_MODE_OUTPUT:       gpioSettings.gpioMode[i] = OUTPUT;       break;
      default:                     gpioSettings.gpioMode[i] = INPUT_PULLUP; break;
    }
  }
  for (int i = 0; i < NUMGPIO; i++) {
    pinMode(gpioSettings.gpioPin[i], gpioSettings.gpioMode[i]);
  }
}

void mqttGPIOCallback(char* topic, char* value, gpioSettingsStruct &gpioSettings) {
  log_message(_F("GPIO: MQTT message received"));
#ifdef ESP32
  if (strcmp_P(PSTR("relay/one"), topic) == 0) {
    log_message(_F("GPIO: MQTT message received 'relay/one'"));
    digitalWrite(relayOnePin, toGPIOValue(value));
    return;
  } else if (strcmp_P(PSTR("relay/two"), topic) == 0) {
    log_message(_F("GPIO: MQTT message received 'relay/two'"));
    digitalWrite(relayTwoPin, toGPIOValue(value));
    return;
  }
#endif
  // Handle user-configurable output GPIOs: topic "extra/N" (1-indexed)
  if (strncmp_P(topic, PSTR("extra/"), 6) == 0) {
    int idx = atoi(topic + 6) - 1;  // convert 1-indexed to 0-indexed
    if (idx >= 0 && idx < NUMGPIO_USER && gpioSettings.gpioMode[idx] == OUTPUT) {
      char log_msg[64];
      snprintf_P(log_msg, sizeof(log_msg), PSTR("GPIO: set extra/%d (pin %d) = %s"), idx + 1, gpioSettings.gpioPin[idx], value);
      log_message(log_msg);
      digitalWrite(gpioSettings.gpioPin[idx], toGPIOValue(value));
    }
  }
}

void publishGPIOStates(PubSubClient &mqtt_client, gpioSettingsStruct &gpioSettings, char* mqtt_topic_base, bool publishAll) {
  static int lastState[NUMGPIO_USER];
  static bool initialized = false;
  if (!initialized) {
    for (int i = 0; i < NUMGPIO_USER; i++) lastState[i] = -1;
    initialized = true;
  }
  char topic[256];
  for (int i = 0; i < NUMGPIO_USER; i++) {
    int state = digitalRead(gpioSettings.gpioPin[i]);
    if (publishAll || state != lastState[i]) {
      lastState[i] = state;
      snprintf_P(topic, sizeof(topic), PSTR("%s/gpio/extra/%d"), mqtt_topic_base, i + 1);
      mqtt_client.publish(topic, state ? "1" : "0", true);
    }
  }
}

void gpioJsonOutput(struct webserver_t *client, gpioSettingsStruct &gpioSettings) {
  char buf[64];
  webserver_send_content_P(client, PSTR("["), 1);
  for (int i = 0; i < NUMGPIO_USER; i++) {
    int state = digitalRead(gpioSettings.gpioPin[i]);
    int len = snprintf_P(buf, sizeof(buf), PSTR("%s{\"pin\":%d,\"mode\":%d,\"state\":%d}"),
                         i > 0 ? "," : "", gpioSettings.gpioPin[i], gpioSettings.gpioUserMode[i], state);
    webserver_send_content(client, buf, len);
  }
  webserver_send_content_P(client, PSTR("]"), 1);
}