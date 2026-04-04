#if defined(ESP8266)
#define NUMGPIO 3
#define NUMGPIO_USER 3  // all GPIOs are user-configurable on ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266WiFiGratuitous.h>
#elif defined(ESP32)
#define NUMGPIO 7
#define NUMGPIO_USER 5  // first 5 are user-configurable; last 2 (pins 21,47) are fixed relays
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Update.h>
#define relayOnePin 21
#define relayTwoPin 47
#endif

// User GPIO mode values (stored in settings)
#define GPIO_MODE_INPUT_PULLUP 0
#define GPIO_MODE_INPUT        1
#define GPIO_MODE_OUTPUT       2

extern const char* mqtt_topic_gpio;

struct gpioSettingsStruct {
#if defined(ESP8266)
  unsigned int gpioPin[NUMGPIO] = {1, 3, 16};
  unsigned int gpioMode[NUMGPIO] = {INPUT_PULLUP, INPUT_PULLUP, INPUT_PULLUP};
#elif defined(ESP32)
  unsigned int gpioPin[NUMGPIO] = {33, 34, 35, 36, 37, 21, 47};
  unsigned int gpioMode[NUMGPIO] = {INPUT_PULLUP, INPUT_PULLUP, INPUT_PULLUP, INPUT_PULLUP, INPUT_PULLUP, OUTPUT, OUTPUT};
#endif
  // User-configurable modes for the non-relay GPIOs (0=INPUT_PULLUP, 1=INPUT, 2=OUTPUT)
  unsigned int gpioUserMode[NUMGPIO_USER] = {};  // all default to 0 (INPUT_PULLUP)
};

class PubSubClient;
struct webserver_t;
void setupGPIO(gpioSettingsStruct &gpioSettings);
void mqttGPIOCallback(char* topic, char* value, gpioSettingsStruct &gpioSettings);
void publishGPIOStates(PubSubClient &mqtt_client, gpioSettingsStruct &gpioSettings, char* mqtt_topic_base, bool publishAll);
void gpioJsonOutput(struct webserver_t *client, gpioSettingsStruct &gpioSettings);


