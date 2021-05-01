#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define MAX_DALLAS_SENSORS 15
#define ONE_WIRE_BUS 4  // DS18B20 pin, for now a static config - should be in config menu later

struct dallasDataStruct {
  float temperature = -127.0;
  unsigned long lastgoodtime = 0;
  DeviceAddress sensor;
  char address[17];
};

void readNewDallasTemp(PubSubClient &mqtt_client, void (*log_message)(char*), char* mqtt_topic_base);
void broadcastDallasTemp(PubSubClient &mqtt_client, void (*log_message)(char*), char* mqtt_topic_base);
void initDallasSensors(void (*log_message)(char*), unsigned int updataAllDallasTimeSettings, unsigned int dallasTimerWaitSettings);
String dallasJsonOutput(void);
String dallasTableOutput(void);
