#define LWIP_INTERNAL

#include <ArduinoJson.h>

#define DATASIZE 203

#define INITIALQUERYSIZE 7
extern byte initialQuery[INITIALQUERYSIZE];
#define PANASONICQUERYSIZE 110
extern byte panasonicQuery[PANASONICQUERYSIZE];


#define OPTDATASIZE 20
#define OPTIONALPCBQUERYTIME 1000 //send optional pcb query each second
#define OPTIONALPCBQUERYSIZE 19
#define OPTIONALPCBSAVETIME 300 //save each 5 minutes the current optional pcb state into flash to have valid values during reboot
extern byte optionalPCBQuery[OPTIONALPCBQUERYSIZE];


extern const char* mqtt_topic_values;
extern const char* mqtt_topic_xvalues;
extern const char* mqtt_topic_commands;
extern const char* mqtt_topic_pcbvalues;
extern const char* mqtt_topic_1wire;
extern const char* mqtt_topic_s0;
extern const char* mqtt_topic_pcb;
extern const char* mqtt_logtopic;
extern const char* mqtt_willtopic;
extern const char* mqtt_iptopic;
extern const char* mqtt_send_raw_value_topic;

// Main heatpump command: cmd[byte_idx] = (byte)(mask | (val * step))
// where val = atoi(msg) is the user-supplied integer value
// mask = bitmask for state 0 (e.g. "off" bit); step = increment per state
struct SimpleCmdDef {
  char name[29];
  char log_name[36];
  uint8_t byte_idx;
  uint8_t mask;
  uint8_t step;
};

// Optional PCB command types
enum OptCmdType : uint8_t {
  OPT_BYTE6_BITS, // optionalPCBQuery[6] bit-masked update
  OPT_TEMP,       // thermistor-encoded temperature to optionalPCBQuery[byte_idx]
  OPT_DIRECT,     // raw byte value to optionalPCBQuery[byte_idx]
};

struct OptCmdDef {
  char name[28];
  char log_name[28];
  OptCmdType type;
  uint8_t byte_idx; // used by OPT_TEMP and OPT_DIRECT
  uint8_t mask;     // bitmask for OPT_BYTE6_BITS
  uint8_t bit;      // bit shift for OPT_BYTE6_BITS
};

void send_heatpump_command(char* topic, char *msg, bool (*send_command)(byte*, int), void (*log_message)(char*), bool optionalPCB);
bool saveOptionalPCB(byte* command, int length);
bool loadOptionalPCB(byte* command, int length);
