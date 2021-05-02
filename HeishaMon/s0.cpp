#include <PubSubClient.h>

#include "src/common/timerqueue.h"
#include "heishamon.h"
#include "commands.h"
#include "s0.h"

#define MQTT_RETAIN_VALUES 1 // do we retain 1wire values?

#define MINREPORTEDS0TIME 5 // how often s0 Watts are reported (not faster than this)

//global array for s0 data
s0DataStruct actS0Data[NUM_S0_COUNTERS];

//global array for s0 Settings
s0SettingsStruct actS0Settings[NUM_S0_COUNTERS];

//volatile pulse detectors for s0
volatile unsigned long new_pulse_s0[2] = {0, 0};


// These are the interrupt routines. Make them as short as possible
// so we don't block other interrupts (for example serial data)
ICACHE_RAM_ATTR void onS0Pulse1() {
  unsigned long new_pulse = millis();
  unsigned long pulseInterval = new_pulse - actS0Data[0].lastPulse;
  if (pulseInterval > 50L) { //50ms debounce filter, this also prevents division by zero to occur a few lines further down the road if pulseInterval = 0
    if (actS0Data[0].lastPulse > 0) { //Do not calculate watt for the first pulse since reboot because we will always report a too high watt. Better to show 0 watt at first pulse.
      actS0Data[0].watt = (3600000000.0 / pulseInterval) / actS0Settings[0].ppkwh;
    }
    actS0Data[0].lastPulse = new_pulse;
    actS0Data[0].pulses++;
  }
}

ICACHE_RAM_ATTR void onS0Pulse2() {
  unsigned long new_pulse = millis();
  unsigned long pulseInterval = new_pulse - actS0Data[1].lastPulse;
  if (pulseInterval > 50L) { //50ms debounce filter, this also prevents division by zero to occur a few lines further down the road if pulseInterval = 0
    if (actS0Data[1].lastPulse > 0) { //Do not calculate watt for the first pulse since reboot because we will always report a too high watt. Better to show 0 watt at first pulse.
      actS0Data[1].watt = (3600000000.0 / pulseInterval) / actS0Settings[1].ppkwh;
    }
    actS0Data[1].lastPulse = new_pulse;
    actS0Data[1].pulses++;
  }
}

void initS0Sensors(s0SettingsStruct s0Settings[], PubSubClient &mqtt_client, char* mqtt_topic_base) {
  char mqtt_topic[256];

  //setup s0 port 1
  actS0Settings[0].gpiopin = s0Settings[0].gpiopin;
  actS0Settings[0].ppkwh = s0Settings[0].ppkwh;
  actS0Settings[0].lowerPowerInterval = s0Settings[0].lowerPowerInterval;
  sprintf_P(mqtt_topic, PSTR("%s/%s/WatthourTotal/1"), mqtt_topic_base, mqtt_topic_s0);
  mqtt_client.subscribe(mqtt_topic);
  pinMode(actS0Settings[0].gpiopin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(actS0Settings[0].gpiopin), onS0Pulse1, RISING);
  timerqueue_insert(MINREPORTEDS0TIME, 0, TIMER_S0_1);

  //setup s0 port 2
  actS0Settings[1].gpiopin = s0Settings[1].gpiopin;
  actS0Settings[1].ppkwh = s0Settings[1].ppkwh;
  actS0Settings[1].lowerPowerInterval = s0Settings[1].lowerPowerInterval;
  pinMode(actS0Settings[1].gpiopin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(actS0Settings[1].gpiopin), onS0Pulse2, RISING);
  timerqueue_insert(MINREPORTEDS0TIME, 0, TIMER_S0_2);
  sprintf_P(mqtt_topic, PSTR("%s/%s/WatthourTotal/2"), mqtt_topic_base, mqtt_topic_s0);
  mqtt_client.subscribe(mqtt_topic);
}

void restore_s0_Watthour(int s0Port, float watthour) {
  if ((s0Port == 1) || (s0Port == 2)) actS0Data[s0Port - 1].pulsesTotal = int(watthour * (actS0Settings[s0Port - 1].ppkwh / 1000.0));
}

void s0SettingsCorrupt(s0SettingsStruct s0Settings[], void (*log_message)(char*)) {
  for (int i = 0 ; i < NUM_S0_COUNTERS ; i++) {
    if ((s0Settings[i].gpiopin != actS0Settings[i].gpiopin) || (s0Settings[i].ppkwh != actS0Settings[i].ppkwh) || (s0Settings[i].lowerPowerInterval != actS0Settings[i].lowerPowerInterval)) {
      char log_msg[256];
      sprintf_P(log_msg, PSTR("S0 settings got corrupted, rebooting!") ); log_message(log_msg);
      delay(1000);
      ESP.restart();
    }
  }
}

void s0Timer(uint8_t i, PubSubClient &mqtt_client, void (*log_message)(char*), char* mqtt_topic_base, s0SettingsStruct s0Settings[]) {

  //check for corruption
  s0SettingsCorrupt(s0Settings, log_message);

  unsigned long millisThisLoop = millis();

  unsigned long lastePulseInterval = millisThisLoop - actS0Data[i].lastPulse;
  unsigned long calcMaxWatt = (3600000000.0 / lastePulseInterval) / actS0Settings[i].ppkwh;

  if (actS0Data[i].watt < ((3600000.0 / actS0Settings[i].ppkwh) / actS0Settings[i].lowerPowerInterval) ) { //watt is lower than possible in lower power interval time
    if(i == 0) {
      timerqueue_insert(actS0Settings[i].lowerPowerInterval, 0, TIMER_S0_1);
    } else {
      timerqueue_insert(actS0Settings[i].lowerPowerInterval, 0, TIMER_S0_2);
    }
    if ((actS0Data[i].watt) / 2 > calcMaxWatt) {
      actS0Data[i].watt = calcMaxWatt / 2;
    }
  }
  else {
    if(i == 0) {
      timerqueue_insert(MINREPORTEDS0TIME, 0, TIMER_S0_1);
    } else {
      timerqueue_insert(MINREPORTEDS0TIME, 0, TIMER_S0_2);
    }

    if (actS0Data[i].watt > calcMaxWatt) {
      actS0Data[i].watt = calcMaxWatt;
    }
  }

  float Watthour = (actS0Data[i].pulses * ( 1000.0 / actS0Settings[i].ppkwh));
  actS0Data[i].pulsesTotal = actS0Data[i].pulsesTotal + actS0Data[i].pulses;
  actS0Data[i].pulses = 0; //per message we report new wattHour, so pulses should be zero at start new message

  //report using mqtt
  char log_msg[256];
  char mqtt_topic[256];
  char valueStr[20];
  sprintf_P(log_msg, PSTR("Measured Watthour on S0 port %d: %.2f"), (i + 1),  Watthour );
  log_message(log_msg);
  sprintf(valueStr, "%.2f", Watthour);
  sprintf_P(mqtt_topic, PSTR("%s/%s/Watthour/%d"), mqtt_topic_base, mqtt_topic_s0, (i + 1));
  mqtt_client.publish(mqtt_topic, valueStr, MQTT_RETAIN_VALUES);
  float WatthourTotal = (actS0Data[i].pulsesTotal * ( 1000.0 / actS0Settings[i].ppkwh));
  sprintf(log_msg, PSTR("Measured total Watthour on S0 port %d: %.2f"), (i + 1),  WatthourTotal );
  log_message(log_msg);
  sprintf(valueStr, "%.2f", WatthourTotal);
  sprintf(mqtt_topic, PSTR("%s/%s/WatthourTotal/%d"), mqtt_topic_base, mqtt_topic_s0, (i + 1));
  mqtt_client.publish(mqtt_topic, valueStr, MQTT_RETAIN_VALUES);
  sprintf(log_msg, PSTR("Calculated Watt on S0 port %d: %u"), (i + 1), actS0Data[i].watt);
  log_message(log_msg);
  sprintf(valueStr, "%u",  actS0Data[i].watt);
  sprintf(mqtt_topic, PSTR("%s/%s/Watt/%d"), mqtt_topic_base, mqtt_topic_s0, (i + 1));
  mqtt_client.publish(mqtt_topic, valueStr, MQTT_RETAIN_VALUES);
}

String s0TableOutput() {
  String output = F("");
  for (int i = 0; i < NUM_S0_COUNTERS; i++) {
    output = output + F("<tr>");
    output = output + F("<td>") + (i + 1) + F("</td>");
    output = output + F("<td>") + actS0Data[i].watt + F("</td>");
    output = output + F("<td>") + (actS0Data[i].pulses * ( 1000.0 / actS0Settings[i].ppkwh)) + F("</td>");
    output = output + F("<td>") + (actS0Data[i].pulsesTotal * ( 1000.0 / actS0Settings[i].ppkwh)) + F("</td>");
    output = output + F("</tr>");
  }
  return output;
}

String s0JsonOutput() {
  String output = F("[");
  for (int i = 0; i < NUM_S0_COUNTERS; i++) {
    output = output + F("{");
    output = output + F("\"S0 port\": \"") + (i + 1) + F("\",");
    output = output + F("\"Watt\": \"") + actS0Data[i].watt + F("\",");
    output = output + F("\"Watthour\": \"") + (actS0Data[i].pulses * ( 1000.0 / actS0Settings[i].ppkwh)) + F("\",");
    output = output + F("\"WatthourTotal\": \"") + (actS0Data[i].pulsesTotal * ( 1000.0 / actS0Settings[i].ppkwh)) + F("\"");
    output = output + F("}");
    if (i < NUM_S0_COUNTERS - 1) output = output + F(",");
  }
  output = output + F("]");
  return output;
}
