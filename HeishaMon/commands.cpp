#include "commands.h"
#include <LittleFS.h>

//removed checksum from default query, is calculated in send_command
byte initialQuery[] = {0x31, 0x05, 0x10, 0x01, 0x00, 0x00, 0x00};
byte panasonicQuery[] = {0x71, 0x6c, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
byte optionalPCBQuery[] = {0xF1, 0x11, 0x01, 0x50, 0x00, 0x00, 0x40, 0xFF, 0xFF, 0xE5, 0xFF, 0xFF, 0x00, 0xFF, 0xEB, 0xFF, 0xFF, 0x00, 0x00};
byte panasonicSendQuery[] PROGMEM = {0xf1, 0x6c, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

#ifdef ESP32
extern QueueHandle_t pcbQueue;
#endif

const char* mqtt_topic_values PROGMEM = "main";
const char* mqtt_topic_xvalues PROGMEM = "extra";
const char* mqtt_topic_commands PROGMEM = "commands";
const char* mqtt_topic_pcbvalues PROGMEM = "optional";
const char* mqtt_topic_1wire PROGMEM = "1wire";
const char* mqtt_topic_s0 PROGMEM = "s0";
const char* mqtt_logtopic PROGMEM = "log";

const char* mqtt_willtopic PROGMEM = "LWT";
const char* mqtt_iptopic PROGMEM = "ip";

const char* mqtt_send_raw_value_topic PROGMEM = "SendRawValue";

static unsigned int temp2hex(float temp) {
  int hextemp = 0;
  if (temp > 120) {
    hextemp = 0;
  } else if (temp < -78) {
    hextemp = 255;
  } else {
    byte Uref = 255;
    int constant = 3695;
    int R25 = 6340;
    byte T25 = 25;
    int Rf = 6480;
    float K = 273.15;
    float RT = R25 * exp(constant * (1 / (temp + K) - 1 / (T25 + K)));
    hextemp = Uref * (RT / (Rf + RT));
  }
  return hextemp;
}

// Table-driven main heatpump commands.
// Encoding: cmd[byte_idx] = (byte)(mask | (val * step))
// where val = atoi(msg) is the integer value sent by the user over MQTT.
//
// mask = bitmask for state 0 (the protocol bits that are always set for this field's "off/zero" state)
// step = bitmask increment per state value
//
// Examples:
//   bool on/off  → mask = off_bits, step = off_bits   e.g. off=0x01, on=0x02 (mask=0x01, step=0x01)
//   signed temp  → mask = 0x80,     step = 1          e.g. 20°C → 0x80|20 = 148
//   multi-state  → mask = step,     step = step       e.g. quiet 0-3 → 0x08/0x10/0x18/0x20
//   force/reset  → mask = 0,        step = bit        e.g. off=0x00, on=bit
const SimpleCmdDef simpleCmds[] PROGMEM = {
  // name,                           log_name,                              idx,  mask,  step
  { "SetHeatpump",                   "heatpump state",                       4,  0x01,  0x01 },
  { "SetPump",                       "pump state",                           4,  0x10,  0x10 },
  { "SetMaxPumpDuty",                "max pump duty",                       45,  0x01,  0x01 },
  // quiet mode: 0=off, 1=level1, 2=level2, 3=level3 → 0x08/0x10/0x18/0x20
  { "SetQuietMode",                  "quiet mode",                           7,  0x08,  0x08 },
  { "SetZ1HeatRequestTemperature",   "z1 heat request temperature",         38,  0x80,  1    },
  { "SetZ1CoolRequestTemperature",   "z1 cool request temperature",         39,  0x80,  1    },
  { "SetZ2HeatRequestTemperature",   "z2 heat request temperature",         40,  0x80,  1    },
  { "SetZ2CoolRequestTemperature",   "z2 cool request temperature",         41,  0x80,  1    },
  { "SetForceDHW",                   "force DHW mode",                       4,  0x40,  0x40 },
  { "SetForceDefrost",               "force defrost mode",                   8,  0x00,  0x02 },
  { "SetForceSterilization",         "force sterilization mode",             8,  0x00,  0x04 },
  { "SetHolidayMode",                "holiday mode",                         5,  0x10,  0x10 },
  // powerful mode: 0=off, 1=30min, 2=60min, 3=90min → 0x01/0x02/0x03/0x04
  { "SetPowerfulMode",               "powerful mode",                        7,  0x01,  0x01 },
  { "SetDHWTemp",                    "DHW temperature",                     42,  0x80,  1    },
  // SetOperationMode is handled separately (bitmask OR — see opModeLookup)
  // zones: 0=zone1, 1=zone2, 2=both → 0x40/0x80/0xC0
  { "SetZones",                      "zones active state",                   6,  0x40,  0x40 },
  { "SetFloorHeatDelta",             "floor heat delta",                    84,  0x80,  1    },
  { "SetFloorCoolDelta",             "floor cool delta",                    94,  0x80,  1    },
  { "SetDHWHeatDelta",               "DHW heat delta",                      99,  0x80,  1    },
  { "SetReset",                      "reset",                                8,  0x00,  0x01 },
  { "SetHeaterDelayTime",            "heater delay time",                  104,  0x01,  0x01 },
  { "SetHeaterStartDelta",           "heater start delta",                 105,  0x80,  1    },
  { "SetHeaterStopDelta",            "heater stop delta",                  106,  0x80,  1    },
  { "SetMainSchedule",               "main schedule",                        5,  0x40,  0x40 },
  { "SetAltExternalSensor",          "alternative external sensor",         20,  0x10,  0x10 },
  // external pad heater: 0=off, 1=type1, 2=type2 → 0x10/0x20/0x30
  { "SetExternalPadHeater",          "external pad heater",                 25,  0x10,  0x10 },
  { "SetBufferDelta",                "buffer tank delta",                   59,  0x80,  1    },
  { "SetBuffer",                     "buffer enabled",                      24,  0x04,  0x04 },
  { "SetHeatingOffOutdoorTemp",      "heating off outdoor temp",            83,  0x80,  1    },
  { "SetExternalControl",            "external control enabled",            23,  0x01,  0x01 },
  { "SetExternalError",              "external error signal enabled",       23,  0x10,  0x10 },
  { "SetExternalCompressorControl",  "external compressor control enabled", 23,  0x40,  0x40 },
  { "SetExternalHeatCoolControl",    "external cool/heat control enabled",  23,  0x04,  0x04 },
  { "SetBivalentControl",            "bivalent control",                    26,  0x01,  0x01 },
  // bivalent mode: 0=alternativ, 1=parallel, 2=advanced parallel → 0x04/0x08/0x0C
  { "SetBivalentMode",               "bivalent mode",                       26,  0x04,  0x04 },
  { "SetBivalentStartTemp",          "bivalent start temperature",          65,  0x80,  1    },
  { "SetBivalentAPStartTemp",        "bivalent ap start temperature",       66,  0x80,  1    },
  { "SetBivalentAPStopTemp",         "bivalent ap stop temperature",        68,  0x80,  1    },
  { "SetHeatingControl",             "heating control",                     30,  0x04,  0x04 },
  { "SetSmartDHW",                   "smart DHW",                           24,  0x40,  0x40 },
  { "SetQuietModePriority",          "quiet mode priority",                 11,  0x10,  0x10 },
  { "SetPumpFlowrateMode",           "pump flowrate mode",                  29,  0x10,  0x10 },
  { "SetDHWSensorSelection",         "DHW sensor selection",                11,  0x01,  0x01 },
  { "SetDHWHeaterState",             "DHW heater state",                     9,  0x04,  0x04 },
  { "SetRoomHeaterState",            "room heater state",                    9,  0x01,  0x01 },
  { "SetHeaterOnOutdoorTemp",        "heater on outdoor temp",              85,  0x80,  1    },
};

// SetOperationMode byte is OR of two bitmask groups:
//   main group: 0x10 = heating/cooling active, 0x20 = DHW active
//   mode  bits: 0x02 = heat, 0x03 = cool, 0x08 = auto, 0x01 = DHW-only
// 0=heat only, 1=cool only, 2=auto, 3=DHW only, 4=heat+DHW, 5=cool+DHW, 6=auto+DHW
const byte opModeLookup[] PROGMEM = {
  0x10|0x02,  // 0: heat only
  0x10|0x03,  // 1: cool only
  0x10|0x08,  // 2: auto
  0x20|0x01,  // 3: DHW only
  0x20|0x02,  // 4: heat+DHW
  0x20|0x03,  // 5: cool+DHW
  0x20|0x08,  // 6: auto+DHW
};

// Optional PCB command table.
// OPT_BYTE6_BITS: bit-mask update of optionalPCBQuery[6]; mask/bit define the field.
// OPT_TEMP:       thermistor-encoded temperature written to optionalPCBQuery[byte_idx].
// OPT_DIRECT:     raw integer byte written to optionalPCBQuery[byte_idx].
const OptCmdDef optCmds[] PROGMEM = {
  // name,                          log_name,                 type,           idx, mask,  bit
  { "SetHeatCoolMode",              "heat cool mode",          OPT_BYTE6_BITS, 0,  0b1,   7  },
  { "SetCompressorState",           "compressor state",        OPT_BYTE6_BITS, 0,  0b1,   6  },
  { "SetSmartGridMode",             "smart grid mode",         OPT_BYTE6_BITS, 0,  0b11,  4  },
  { "SetExternalThermostat1State",  "external thermostat 1",   OPT_BYTE6_BITS, 0,  0b11,  2  },
  { "SetExternalThermostat2State",  "external thermostat 2",   OPT_BYTE6_BITS, 0,  0b11,  0  },
  { "SetDemandControl",             "demand control",          OPT_DIRECT,    14,  0,     0  },
  { "SetPoolTemp",                  "pool temp",               OPT_TEMP,       7,  0,     0  },
  { "SetBufferTemp",                "buffer temp",             OPT_TEMP,       8,  0,     0  },
  { "SetZ1RoomTemp",                "z1 room temp",            OPT_TEMP,      10,  0,     0  },
  { "SetZ1WaterTemp",               "z1 water temp",           OPT_TEMP,      16,  0,     0  },
  { "SetZ2RoomTemp",                "z2 room temp",            OPT_TEMP,      11,  0,     0  },
  { "SetZ2WaterTemp",               "z2 water temp",           OPT_TEMP,      15,  0,     0  },
  { "SetSolarTemp",                 "solar temp",              OPT_TEMP,      13,  0,     0  },
  { "SetOptPCBByte9",               "byte 9",                  OPT_DIRECT,     9,  0,     0  },
};

static unsigned int handle_curves(char *msg, unsigned char *cmd, char *log_msg) {
  memcpy_P(cmd, panasonicSendQuery, sizeof(panasonicSendQuery));

  JsonDocument jsonDoc;
  DeserializationError error = deserializeJson(jsonDoc, msg);
  if (!error) {
    snprintf(log_msg, 255, "SetCurves JSON received ok");
    JsonVariant jsonValue;
    //set correct bytes according to the values in json and if not exists keep default 0x00 value which keeps current setting for this byte
    jsonValue = jsonDoc["zone1"]["heat"]["target"]["high"]; if (!jsonValue.isNull()) cmd[75] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone1"]["heat"]["target"]["low"];  if (!jsonValue.isNull()) cmd[76] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone1"]["heat"]["outside"]["low"]; if (!jsonValue.isNull()) cmd[77] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone1"]["heat"]["outside"]["high"];if (!jsonValue.isNull()) cmd[78] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["heat"]["target"]["high"]; if (!jsonValue.isNull()) cmd[79] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["heat"]["target"]["low"];  if (!jsonValue.isNull()) cmd[80] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["heat"]["outside"]["low"]; if (!jsonValue.isNull()) cmd[81] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["heat"]["outside"]["high"];if (!jsonValue.isNull()) cmd[82] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone1"]["cool"]["target"]["high"]; if (!jsonValue.isNull()) cmd[86] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone1"]["cool"]["target"]["low"];  if (!jsonValue.isNull()) cmd[87] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone1"]["cool"]["outside"]["low"]; if (!jsonValue.isNull()) cmd[88] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone1"]["cool"]["outside"]["high"];if (!jsonValue.isNull()) cmd[89] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["cool"]["target"]["high"]; if (!jsonValue.isNull()) cmd[90] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["cool"]["target"]["low"];  if (!jsonValue.isNull()) cmd[91] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["cool"]["outside"]["low"]; if (!jsonValue.isNull()) cmd[92] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["cool"]["outside"]["high"];if (!jsonValue.isNull()) cmd[93] = jsonValue.as<int>() + 128;
  } else {
    snprintf(log_msg, 255, "SetCurves JSON decode failed!");
    return 0;
  }

  return sizeof(panasonicSendQuery);
}

void send_heatpump_command(char* topic, char *msg, bool (*send_command)(byte*, int), void (*log_message)(char*), bool optionalPCB) {
  unsigned char cmd[256] = { 0 };
  char log_msg[256] = { 0 };
  int val = atoi(msg);

  // Bitmask-encoded commands: cmd[byte_idx] = mask | (val * step)
  for (unsigned int i = 0; i < sizeof(simpleCmds) / sizeof(simpleCmds[0]); i++) {
    SimpleCmdDef def;
    memcpy_P(&def, &simpleCmds[i], sizeof(def));
    if (strcmp(topic, def.name) == 0) {
      memcpy_P(cmd, panasonicSendQuery, sizeof(panasonicSendQuery));
      cmd[def.byte_idx] = (byte)(def.mask | (val * def.step));
      snprintf_P(log_msg, 255, PSTR("set %s to %d"), def.log_name, val);
      log_message(log_msg);
      send_command(cmd, sizeof(panasonicSendQuery));
      return;
    }
  }

  // SetOperationMode: non-linear mapping via lookup table
  if (strcmp(topic, "SetOperationMode") == 0) {
    if (val >= 0 && val <= 6) {
      memcpy_P(cmd, panasonicSendQuery, sizeof(panasonicSendQuery));
      cmd[6] = pgm_read_byte(&opModeLookup[val]);
      snprintf_P(log_msg, 255, PSTR("set heat pump mode to %d"), val);
      log_message(log_msg);
      send_command(cmd, sizeof(panasonicSendQuery));
    }
    return;
  }

  // SetCurves: JSON payload with multiple bytes
  if (strcmp(topic, "SetCurves") == 0) {
    unsigned int len = handle_curves(msg, cmd, log_msg);
    log_message(log_msg);
    if (len > 0) send_command(cmd, len);
    return;
  }

  // Optional PCB commands
  if (optionalPCB) {
    for (unsigned int i = 0; i < sizeof(optCmds) / sizeof(optCmds[0]); i++) {
      OptCmdDef def;
      memcpy_P(&def, &optCmds[i], sizeof(def));
      if (strcmp(topic, def.name) != 0) continue;

      switch (def.type) {
        case OPT_BYTE6_BITS: {
          int clamped = val & def.mask;
          optionalPCBQuery[6] = (optionalPCBQuery[6] & ~(def.mask << def.bit)) | (clamped << def.bit);
          snprintf_P(log_msg, 255, PSTR("set optional pcb '%s' to %d (byte 6: %02x)"), def.log_name, clamped, optionalPCBQuery[6]);
          break;
        }
        case OPT_TEMP: {
          float temp = atof(msg);
          byte hex = temp2hex(temp);
          optionalPCBQuery[def.byte_idx] = hex;
          snprintf_P(log_msg, 255, PSTR("set optional pcb '%s' to %.2f (%02x)"), def.log_name, temp, hex);
          break;
        }
        case OPT_DIRECT: {
          optionalPCBQuery[def.byte_idx] = (byte)val;
          snprintf_P(log_msg, 255, PSTR("set optional pcb '%s' to %02x"), def.log_name, (byte)val);
          break;
        }
      }

      log_message(log_msg);
#ifdef ESP32
      xQueueOverwrite(pcbQueue, optionalPCBQuery);
#endif
      return;
    }
  }
}


bool saveOptionalPCB(byte* command, int length) {
  if (LittleFS.begin()) {
    File pcbfile = LittleFS.open("/optionalpcb.raw", "w");
    if (pcbfile) {
      pcbfile.write(command, length);
      pcbfile.close();
      return true;
    }

  }
  return false;
}
bool loadOptionalPCB(byte* command, int length) {
  if (LittleFS.begin()) {
    if (LittleFS.exists("/optionalpcb.raw")) {
      File pcbfile = LittleFS.open("/optionalpcb.raw", "r");
      if (pcbfile) {
        pcbfile.read(command, length);
        pcbfile.close();
        return true;
      }
    }
  }
  return false;
}
