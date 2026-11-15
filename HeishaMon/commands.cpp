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


static bool installerCommandsAllowed = false;

void setInstallerCommandsAllowed(bool allowed) {
  installerCommandsAllowed = allowed;
}

// The whole payload must be a number: an optional minus and digits, with tenths also one decimal ("3.5").
// The value is returned as an integer, or in tenths.
static bool strict_number(const char *msg, bool tenths, long &value) {
  const size_t length = msg == NULL ? 0 : strlen(msg);
  if (length == 0 || length > (tenths ? 7u : 5u)) return false;
  const bool negative = msg[0] == '-';
  size_t index = negative ? 1 : 0;
  if (index == length || msg[index] < '0' || msg[index] > '9') return false;
  value = 0;
  for (; index < length && msg[index] >= '0' && msg[index] <= '9'; ++index) {
    value = value * 10 + msg[index] - '0';
  }
  if (tenths) {
    value *= 10;
    if (index < length) {
      if (msg[index] != '.' || index + 2 != length || msg[index + 1] < '0' || msg[index + 1] > '9') return false;
      value += msg[index + 1] - '0';
      index += 2;
    }
  }
  if (index != length) return false;
  if (negative) value = -value;
  return true;
}

// Build the datagram of a main command from its payload, as described by its row in commands[]. A refused
// payload returns 0 and leaves cmd untouched.
unsigned int heatpump_command(const cmdStruct *command, char *msg, unsigned char *cmd, char *log_msg) {
  if ((command->flags & CMD_INSTALLER) && !installerCommandsAllowed) {
    snprintf_P(log_msg, 256, PSTR("%s ignored: installer commands are not allowed"), command->name);
    return 0;
  }
  if (command->type == CMD_FUNC) return command->func(msg, cmd, log_msg);

  long value = 0;
  const bool tenths = command->type == CMD_TENTHS;
  if (command->type == CMD_STRICT || tenths) {
    if (!strict_number(msg, tenths, value) || value < command->minimum || value > command->maximum || value % command->step != 0) {
      if (tenths) {
        snprintf_P(log_msg, 256, PSTR("%s ignored: expected %.1f..%.1f, step %.1f"), command->name,
                   command->minimum / 10.0, command->maximum / 10.0, command->step / 10.0);
      } else {
        snprintf_P(log_msg, 256, PSTR("%s ignored: expected integer %d..%d, step %d"), command->name,
                   (int)command->minimum, (int)command->maximum, (int)command->step);
      }
      return 0;
    }
  } else {
    if (msg != NULL) value = atol(msg);
    if (command->type == CMD_CHOICE && (value < 0 || value > command->maximum)) value = 0;
  }

  memcpy_P(cmd, panasonicSendQuery, sizeof(panasonicSendQuery));
  cmd[command->address] = (byte)(((value / command->divisor + command->offset) & command->mask) << command->shift);
  // log the value that is sent: a number outside the field wraps, a choice outside its range becomes 0
  value = ((long)((cmd[command->address] >> command->shift) & command->mask) - command->offset) * command->divisor;
  if (tenths) {
    snprintf_P(log_msg, 256, PSTR("%s set to %.1f"), command->name, value / 10.0);
  } else {
    snprintf_P(log_msg, 256, PSTR("%s set to %ld"), command->name, value);
  }
  return sizeof(panasonicSendQuery);
}

// The commands below can't be described by a row of settings in commands[].

unsigned int set_operation_mode(char *msg, unsigned char *cmd, char *log_msg) {

  String set_mode_string(msg);

  byte set_mode;
  switch (set_mode_string.toInt()) {
    case 0: set_mode = 18; break;
    case 1: set_mode = 19; break;
    case 2: set_mode = 24; break;
    case 3: set_mode = 33; break;
    case 4: set_mode = 34; break;
    case 5: set_mode = 35; break;
    case 6: set_mode = 40; break;
    default: set_mode = 0; break;
  }

  {
    char tmp[256] = { 0 };
    snprintf_P(tmp, 255, PSTR("set heat pump mode to %d"), set_mode);
    memcpy(log_msg, tmp, sizeof(tmp));
  }

  {
    memcpy_P(cmd, panasonicSendQuery, sizeof(panasonicSendQuery));
    cmd[6] = set_mode;
  }

  return sizeof(panasonicSendQuery);
}

unsigned int set_curves(char *msg, unsigned char *cmd, char *log_msg) {
  memcpy_P(cmd, panasonicSendQuery, sizeof(panasonicSendQuery));

  JsonDocument jsonDoc;
  DeserializationError error = deserializeJson(jsonDoc, msg);
  if (!error) {
    char tmpmsg[256] = { 0 };
    JsonVariant jsonValue;
    snprintf(tmpmsg, 255, "SetCurves JSON received ok");
    memcpy(log_msg, tmpmsg, sizeof(tmpmsg));
    //set correct bytes according to the values in json and if not exists keep default 0x00 value which keeps current setting for this byte
    jsonValue = jsonDoc["zone1"]["heat"]["target"]["high"]; if (!jsonValue.isNull()) cmd[75] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone1"]["heat"]["target"]["low"]; if (!jsonValue.isNull()) cmd[76] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone1"]["heat"]["outside"]["low"]; if (!jsonValue.isNull()) cmd[77] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone1"]["heat"]["outside"]["high"]; if (!jsonValue.isNull()) cmd[78] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["heat"]["target"]["high"]; if (!jsonValue.isNull()) cmd[79] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["heat"]["target"]["low"]; if (!jsonValue.isNull()) cmd[80] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["heat"]["outside"]["low"]; if (!jsonValue.isNull()) cmd[81] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["heat"]["outside"]["high"]; if (!jsonValue.isNull()) cmd[82] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone1"]["cool"]["target"]["high"]; if (!jsonValue.isNull()) cmd[86] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone1"]["cool"]["target"]["low"]; if (!jsonValue.isNull()) cmd[87] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone1"]["cool"]["outside"]["low"]; if (!jsonValue.isNull()) cmd[88] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone1"]["cool"]["outside"]["high"]; if (!jsonValue.isNull()) cmd[89] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["cool"]["target"]["high"]; if (!jsonValue.isNull()) cmd[90] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["cool"]["target"]["low"]; if (!jsonValue.isNull()) cmd[91] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["cool"]["outside"]["low"]; if (!jsonValue.isNull()) cmd[92] = jsonValue.as<int>() + 128;
    jsonValue = jsonDoc["zone2"]["cool"]["outside"]["high"]; if (!jsonValue.isNull()) cmd[93] = jsonValue.as<int>() + 128;
  } else {
    char tmpmsg[256] = { 0 };
    snprintf(tmpmsg, 255, "SetCurves JSON decode failed!");
    memcpy(log_msg, tmpmsg, sizeof(tmpmsg));
  }

  return sizeof(panasonicSendQuery);
}

unsigned int set_zones(char *msg, unsigned char *cmd, char *log_msg) {

  String set_mode_string(msg);

  byte set_mode;
  switch (set_mode_string.toInt()) {
    case 0: set_mode = 64; break;
    case 1: set_mode = 128; break;
    case 2: set_mode = 192; break;
    default: set_mode = 0; break;
  }

  {
    char tmp[256] = { 0 };
    snprintf_P(tmp, 255, PSTR("set zones active state to %d"), set_mode);
    memcpy(log_msg, tmp, sizeof(tmp));
  }

  {
    memcpy_P(cmd, panasonicSendQuery, sizeof(panasonicSendQuery));
    cmd[6] = set_mode;
  }

  return sizeof(panasonicSendQuery);
}

// The reverse of the commands above: what does a command datagram ask for?

// The curve points of a datagram as the JSON that SetCurves takes, only the points that are set.
static void curves_json(const unsigned char *cmd, char *json, size_t size) {
  size_t used = 0;
  bool anyZone = false;
  auto add = [&](const char *text) {
    if (used < size) used += snprintf(json + used, size - used, "%s", text);
  };
  add("{");
  for (int zone = 0; zone < 2; zone++) {
    bool zoneOpen = false;
    for (int cool = 0; cool < 2; cool++) {
      bool modeOpen = false;
      for (int outside = 0; outside < 2; outside++) {
        bool groupOpen = false;
        for (int point = 0; point < 2; point++) {
          const unsigned char value = cmd[(cool ? 86 : 75) + zone * 4 + outside * 2 + point];
          if (value == 0) continue;
          if (!zoneOpen) {
            if (anyZone) add(",");
            add(zone ? "\"zone2\":{" : "\"zone1\":{");
          }
          if (!modeOpen) {
            if (zoneOpen) add(",");
            add(cool ? "\"cool\":{" : "\"heat\":{");
          }
          if (groupOpen || modeOpen) add(",");
          if (!groupOpen) add(outside ? "\"outside\":{" : "\"target\":{");
          // target is stored high, low and outside low, high
          add(outside == point ? "\"high\":" : "\"low\":");
          char number[8];
          snprintf(number, sizeof(number), "%d", value - 128);
          add(number);
          zoneOpen = modeOpen = groupOpen = anyZone = true;
        }
        if (groupOpen) add("}");
      }
      if (modeOpen) add("}");
    }
    if (zoneOpen) add("}");
  }
  add("}");
}

// Report every setting a command datagram holds, as the command name and the payload that builds it. Bits that
// no command describes are reported as "Byte<n>" with their value in hex. Returns the number of reports.
unsigned int decode_command(const unsigned char *cmd, unsigned int length, void (*report)(const char *name, const char *value)) {
  if (length != sizeof(panasonicSendQuery)) return 0;
  unsigned char known[sizeof(panasonicSendQuery)] = { 0 };  // the bits a command describes
  char value[400];
  unsigned int count = 0;

  for (unsigned int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    cmdStruct command;
    memcpy_P(&command, &commands[i], sizeof(command));
    if (command.type == CMD_FUNC) continue;
    known[command.address] |= (unsigned char)(command.mask << command.shift);
    const long field = (cmd[command.address] >> command.shift) & command.mask;
    if (field == 0) continue;  // not set
    const long number = (field - command.offset) * command.divisor;
    if (command.type == CMD_TENTHS) {
      snprintf_P(value, sizeof(value), PSTR("%.1f"), number / 10.0);
    } else {
      snprintf_P(value, sizeof(value), PSTR("%ld"), number);
    }
    report(command.name, value);
    count++;
  }

  // SetOperationMode and SetZones share byte 6
  static const unsigned char modes[] = { 18, 19, 24, 33, 34, 35, 40 };
  for (unsigned int mode = 0; mode < sizeof(modes); mode++) {
    if ((cmd[6] & 0x3F) != modes[mode]) continue;
    snprintf_P(value, sizeof(value), PSTR("%u"), mode);
    report("SetOperationMode", value);
    count++;
    known[6] |= 0x3F;
  }
  if (cmd[6] & 0xC0) {
    snprintf_P(value, sizeof(value), PSTR("%d"), (cmd[6] >> 6) - 1);
    report("SetZones", value);
    count++;
  }
  known[6] |= 0xC0;

  // SetCurves: z1 and z2 heat in bytes 75-82, z1 and z2 cool in bytes 86-93
  bool curves = false;
  for (unsigned int i = 75; i <= 93; i++) {
    if (i >= 83 && i <= 85) continue;
    known[i] = 0xFF;
    if (cmd[i] != 0) curves = true;
  }
  if (curves) {
    curves_json(cmd, value, sizeof(value));
    report("SetCurves", value);
    count++;
  }

  for (unsigned int i = 4; i < length; i++) {  // the first four bytes are the header
    const unsigned char unknown = cmd[i] & ~known[i];
    if (unknown == 0) continue;
    char name[8];
    snprintf_P(name, sizeof(name), PSTR("Byte%u"), i);
    snprintf_P(value, sizeof(value), PSTR("0x%02X"), unknown);
    report(name, value);
    count++;
  }
  return count;
}

//start of optional pcb commands
unsigned int set_byte_6(int val, int base, int bit, char *log_msg, const char *func) {
  unsigned char hex = (optionalPCBQuery[6] & ~(base << bit)) | (val << bit);

  {
    char tmp[256] = { 0 };
    snprintf_P(tmp, 255, PSTR("set optional pcb '%s' to state %d (result byte 6: %02x)"), func, val, hex);
    memcpy(log_msg, tmp, sizeof(tmp));
  }

  {
    optionalPCBQuery[6] = hex;
  }

  return sizeof(optionalPCBQuery);
}

unsigned int set_byte_9(char *msg, char *log_msg) {
  String set_pcb_string(msg);

  byte set_pcb_value = set_pcb_string.toInt();

  {
    char tmp[256] = { 0 };
    snprintf_P(tmp, 255, PSTR("set optional pcb '%s' to %02x"), __FUNCTION__, set_pcb_value);
    memcpy(log_msg, tmp, sizeof(tmp));
  }

  {
    optionalPCBQuery[9] = set_pcb_value;
  }
  return sizeof(optionalPCBQuery);
}

unsigned int set_heat_cool_mode(char *msg, char *log_msg) {
  String set_pcb_string(msg);
  int set_pcb_value = (set_pcb_string.toInt() == 1);

  return set_byte_6(set_pcb_value, 0b1, 7, log_msg, __FUNCTION__);
}

unsigned int set_compressor_state(char *msg, char *log_msg) {
  String set_pcb_string(msg);
  int set_pcb_value = (set_pcb_string.toInt() == 1);

  return set_byte_6(set_pcb_value, 0b1, 6, log_msg, __FUNCTION__);
}

unsigned int set_smart_grid_mode(char *msg, char *log_msg) {
  String set_pcb_string(msg);
  int set_pcb_value = set_pcb_string.toInt();

  if (set_pcb_value < 4) {
    return set_byte_6(set_pcb_value, 0b11, 4, log_msg, __FUNCTION__);
  } else {
    return 0;
  }
}

unsigned int set_external_thermostat_1_state(char *msg, char *log_msg) {
  String set_pcb_string(msg);
  int set_pcb_value = set_pcb_string.toInt();

  if (set_pcb_value < 4) {
    return set_byte_6(set_pcb_value, 0b11, 2, log_msg, __FUNCTION__);
  } else {
    return 0;
  }
}

unsigned int set_external_thermostat_2_state(char *msg, char *log_msg) {
  String set_pcb_string(msg);
  int set_pcb_value = set_pcb_string.toInt();

  if (set_pcb_value < 4) {
    return set_byte_6(set_pcb_value, 0b11, 0, log_msg, __FUNCTION__);
  } else {
    return 0;
  }
}

unsigned int set_demand_control(char *msg, char *log_msg) {
  String set_pcb_string(msg);

  byte set_pcb_value = set_pcb_string.toInt();

  {
    char tmp[256] = { 0 };
    snprintf_P(tmp, 255, PSTR("set optional pcb '%s' to %02x"), __FUNCTION__, set_pcb_value);
    memcpy(log_msg, tmp, sizeof(tmp));
  }

  {
    optionalPCBQuery[14] = set_pcb_value;
  }

  return sizeof(optionalPCBQuery);
}

unsigned int set_xxx_temp(char *msg, char *log_msg, int byte, const char *func) {
  String set_pcb_string(msg);

  float temp = set_pcb_string.toFloat();

  {
    char tmp[256] = { 0 };
    snprintf_P(tmp, 255, PSTR("set optional pcb '%s' to temp %.2f (%02x)"), func, temp, temp2hex(temp));
    memcpy(log_msg, tmp, sizeof(tmp));
  }

  {
    optionalPCBQuery[byte] = temp2hex(temp);
  }

  return sizeof(optionalPCBQuery);
}

unsigned int set_pool_temp(char *msg, char *log_msg) {
  return set_xxx_temp(msg, log_msg, 7, __FUNCTION__);
}

unsigned int set_buffer_temp(char *msg, char *log_msg) {
  return set_xxx_temp(msg, log_msg, 8, __FUNCTION__);
}

unsigned int set_z1_room_temp(char *msg, char *log_msg) {
  return set_xxx_temp(msg, log_msg, 10, __FUNCTION__);
}

unsigned int set_z1_water_temp(char *msg, char *log_msg) {
  return set_xxx_temp(msg, log_msg, 16, __FUNCTION__);
}

unsigned int set_z2_room_temp(char *msg, char *log_msg) {
  return set_xxx_temp(msg, log_msg, 11, __FUNCTION__);
}

unsigned int set_z2_water_temp(char *msg, char *log_msg) {
  return set_xxx_temp(msg, log_msg, 15, __FUNCTION__);
}

unsigned int set_solar_temp(char *msg, char *log_msg) {
  return set_xxx_temp(msg, log_msg, 13, __FUNCTION__);
}




void send_heatpump_command(char* topic, char *msg, bool (*send_command)(byte*, int), void (*log_message)(char*), bool optionalPCB) {
  unsigned char cmd[256] = { 0 };
  char log_msg[256] = { 0 };
  unsigned int len = 0;

  for (unsigned int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    cmdStruct tmp;
    memcpy_P(&tmp, &commands[i], sizeof(tmp));
    if (strcmp(topic, tmp.name) == 0) {
      len = heatpump_command(&tmp, msg, cmd, log_msg);
      log_message(log_msg);
      if (len > 0) send_command(cmd, len);
    }
  }

  if (optionalPCB) {
    //run for optional pcb commands
    for (unsigned int i = 0; i < sizeof(optionalCommands) / sizeof(optionalCommands[0]); i++) {
      optCmdStruct tmp;
      memcpy_P(&tmp, &optionalCommands[i], sizeof(tmp));
      if (strcmp(topic, tmp.name) == 0) {
        len = tmp.func(msg, log_msg);
        log_message(log_msg);
#ifdef ESP32        
       xQueueOverwrite(pcbQueue, optionalPCBQuery);
#endif          
      }
    }
  }

}


// The optional PCB state is saved every OPTIONALPCBSAVETIME seconds; only a
// changed state is written, so an unchanged one causes no flash wear.
static byte optionalPCBSaved[OPTIONALPCBQUERYSIZE];
static bool optionalPCBSavedValid = false;

static void rememberOptionalPCB(const byte* command, int length) {
  if (length != OPTIONALPCBQUERYSIZE) return;
  memcpy(optionalPCBSaved, command, OPTIONALPCBQUERYSIZE);
  optionalPCBSavedValid = true;
}

bool saveOptionalPCB(byte* command, int length) {
  if (optionalPCBSavedValid && length == OPTIONALPCBQUERYSIZE && memcmp(command, optionalPCBSaved, OPTIONALPCBQUERYSIZE) == 0) {
    return true;  // unchanged since the last save or load
  }
  if (LittleFS.begin()) {
    File pcbfile = LittleFS.open("/optionalpcb.raw", "w");
    if (pcbfile) {
      pcbfile.write(command, length);
      pcbfile.close();
      rememberOptionalPCB(command, length);
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
        rememberOptionalPCB(command, length);
        return true;
      }
    }
  }
  return false;
}
