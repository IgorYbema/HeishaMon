"""Test the new setters and real command dispatch with g++ on the host.

Run: python3 Tools/test_commands.py
"""

import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# The byte every main command sends for the payloads below: (byte, [value per payload]). None means the
# payload is refused and nothing is sent. 0 is sent as a datagram that changes nothing. These values were
# recorded from the per-command functions the table in commands.h replaced, so that table has to keep
# producing exactly the same datagrams. SetCurves takes JSON and is not covered here.
PAYLOADS = ["", "0", "1", "2", "3", "6", "-5", "21", "60", "75", "127", "255", "300", "1.0", "21.5", " 1", "1x", "abc"]
RECORDED = {
    "SetHeatpump": (4, [1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 2, 1]),
    "SetPump": (4, [16, 16, 32, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 32, 16, 32, 32, 16]),
    "SetMaxPumpDuty": (45, [1, 1, 2, 3, 4, 7, 252, 22, 61, 76, 128, 0, 45, 2, 22, 2, 2, 1]),
    # not as recorded: the old function let a value above 6 run into the quiet mode schedule bits of the same byte
    "SetQuietMode": (7, [8, 8, 16, 24, 32, 56, 32, 48, 40, 32, 0, 0, 40, 16, 48, 16, 16, 8]),
    "SetZ1HeatRequestTemperature": (38, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetZ1CoolRequestTemperature": (39, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetZ2HeatRequestTemperature": (40, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetZ2CoolRequestTemperature": (41, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetForceDHW": (4, [64, 64, 128, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 128, 64, 128, 128, 64]),
    "SetForceDefrost": (8, [0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 2, 2, 0]),
    "SetForceSterilization": (8, [0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 4, 4, 0]),
    "SetSterilizationTemp": (100, [None, None, None, None, None, None, None, None, 188, 203, None, None, None, None, None, None, None, None]),
    "SetSterilizationMaxTime": (101, [None, None, None, None, None, None, None, None, 61, None, None, None, None, None, None, None, None, None]),
    "SetForceHeater": (5, [4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 4, 8, 8, 4]),
    "SetHolidayMode": (5, [16, 16, 32, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 32, 16, 32, 32, 16]),
    "SetPowerfulMode": (7, [1, 1, 2, 3, 4, 7, 4, 6, 5, 4, 0, 0, 5, 2, 6, 2, 2, 1]),
    "SetOperationMode": (6, [18, 18, 19, 24, 33, 40, 0, 0, 0, 0, 0, 0, 0, 19, 0, 19, 19, 18]),
    "SetDHWTemp": (42, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetZones": (6, [64, 64, 128, 192, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 0, 128, 128, 64]),
    "SetFloorHeatDelta": (84, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetFloorCoolDelta": (94, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetDHWHeatDelta": (99, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetReset": (8, [0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0]),
    "SetHeaterDelayTime": (104, [1, 1, 2, 3, 4, 7, 252, 22, 61, 76, 128, 0, 45, 2, 22, 2, 2, 1]),
    "SetHeaterStartDelta": (105, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetHeaterStopDelta": (106, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetMainSchedule": (5, [64, 64, 128, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 128, 64, 128, 128, 64]),
    "SetAltExternalSensor": (20, [16, 16, 32, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 32, 16, 32, 32, 16]),
    "SetExternalPadHeater": (25, [16, 16, 32, 48, 16, 16, 16, 16, 16, 16, 16, 16, 16, 32, 16, 32, 32, 16]),
    "SetBufferDelta": (59, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetBuffer": (24, [4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 4, 8, 8, 4]),
    "SetHeatingOffOutdoorTemp": (83, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetExternalControl": (23, [1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 2, 1]),
    "SetExternalError": (23, [16, 16, 32, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 32, 16, 32, 32, 16]),
    "SetExternalCompressorControl": (23, [64, 64, 128, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 128, 64, 128, 128, 64]),
    "SetExternalHeatCoolControl": (23, [4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 4, 8, 8, 4]),
    "SetBivalentControl": (26, [1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 2, 1]),
    "SetBivalentMode": (26, [4, 4, 8, 12, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 4, 8, 8, 4]),
    "SetBivalentStartTemp": (65, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetBivalentAPStartTemp": (66, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetBivalentAPStopTemp": (68, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetHeatingControl": (30, [4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 4, 8, 8, 4]),
    "SetSmartDHW": (24, [64, 64, 128, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 128, 64, 128, 128, 64]),
    "SetQuietModePriority": (11, [16, 16, 32, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 32, 16, 32, 32, 16]),
    "SetPumpFlowrateMode": (29, [16, 16, 32, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 32, 16, 32, 32, 16]),
    "SetDHWSensorSelection": (11, [1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 2, 1]),
    "SetDHWHeaterState": (9, [4, 4, 8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 4, 8, 8, 4]),
    "SetRoomHeaterState": (9, [1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 2, 1]),
    "SetHeaterOnOutdoorTemp": (85, [128, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, 127, 172, 129, 149, 129, 129, 128]),
    "SetHeatToCoolTemp": (95, [None, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, None, None, None, None, None, None, None]),
    "SetCoolToHeatTemp": (96, [None, 128, 129, 130, 131, 134, 123, 149, 188, 203, 255, None, None, None, None, None, None, None]),
    "SetRoomOperationMaxTime": (97, [None, None, None, None, None, None, None, None, 3, None, None, None, 11, None, None, None, None, None]),
    "SetDHWHeatupMaxTime": (98, [None, None, 2, 3, 4, 7, None, 22, 61, 76, 128, None, None, None, None, None, None, None]),
    "SetQuietModeSchedule": (7, [None, 64, 128, None, None, None, None, None, None, None, None, None, None, None, None, None, None, None]),
}

# Checked commands: (byte, [(payload, value)], [refused payloads])
CHECKED = {
    "SetHeatToCoolTemp": (
        95,
        [("21", 149), ("-127", 1), ("127", 255), ("0", 128)],
        ["-128", "128"],
    ),
    "SetCoolToHeatTemp": (
        96,
        [("12", 140), ("-127", 1), ("127", 255)],
        ["-128", "128"],
    ),
    "SetRoomOperationMaxTime": (
        97,
        [("90", 4), ("30", 2), ("7620", 255)],
        ["0", "29", "31", "7650"],
    ),
    "SetDHWHeatupMaxTime": (
        98,
        [("105", 106), ("1", 2), ("254", 255)],
        ["0", "255", "-1"],
    ),
    "SetQuietModeSchedule": (7, [("0", 64), ("1", 128)], ["2", "-1"]),
    "SetSterilizationTemp": (100, [("55", 183), ("75", 203)], ["54", "76", "-55"]),
    "SetSterilizationMaxTime": (101, [("5", 6), ("60", 61)], ["0", "4", "7", "65"]),
}
REFUSED = ["", "abc", "1x", "1.0", " 1", "+1", "1 ", "-", "999999999999999"]

SHIM = r"""
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
using byte = unsigned char;
#define PROGMEM
#define memcpy_P memcpy
#define snprintf_P snprintf
#define PSTR(x) x
struct String {
  std::string s;
  String(const char *c) : s(c ? c : "") {}
  long toInt() const { return atol(s.c_str()); }
};
std::vector<byte> sent;
int send_count;
bool send_command(byte* data, int length) {
  ++send_count; sent.assign(data, data + length); return true;
}
void log_message(char*) {}
std::vector<std::pair<std::string, std::string>> reports;
void report(const char *name, const char *value) { reports.emplace_back(name, value); }
"""

# one command through the real dispatcher: value None = refused, nothing sent and the buffer left untouched
CASE = r"""
  {{ char topic[] = "{topic}", payload[] = "{payload}";
    sent.clear(); send_count = 0;
    send_heatpump_command(topic, payload, send_command, log_message, false);
    check(topic, payload, {address}, {value});
  }}
"""

MAIN = r"""
static const cmdStruct *find(const char *topic) {
  for (unsigned int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
    if (strcmp(commands[i].name, topic) == 0) return &commands[i];
  return nullptr;
}

static void fail(const char *topic, const char *payload, const char *why) {
  printf("FAIL %s '%s': %s\n", topic, payload, why);
  exit(1);
}

static void check(const char *topic, char *payload, int address, int value) {
  const cmdStruct *command = find(topic);
  if (!command) fail(topic, payload, "not in commands[]");
  if (value < 0) {
    if (send_count != 0) fail(topic, payload, "sent, expected refused");
    byte buffer[256]; memset(buffer, 0xa5, sizeof(buffer));
    char log[256];
    if (heatpump_command(command, payload, buffer, log) != 0) fail(topic, payload, "length not 0");
    for (byte b : buffer) if (b != 0xa5) fail(topic, payload, "buffer touched");
    return;
  }
  if (send_count != 1 || sent.size() != sizeof(panasonicSendQuery)) fail(topic, payload, "not sent once");
  for (unsigned int i = 0; i < sent.size(); ++i) {
    byte expected = (int)i == address ? value : panasonicSendQuery[i];
    if (sent[i] != expected) {
      printf("FAIL %s '%s': byte %u is 0x%02X, expected 0x%02X\n", topic, payload, i, sent[i], expected);
      exit(1);
    }
  }
}

int main() {
"""

# an installer command is refused until installer commands are allowed, a null payload never crashes
GATE = r"""
  { const cmdStruct installer = { "SetTest", CMD_INSTALLER, CMD_ONOFF(4, 0) };
    byte buffer[256]; char log[256]; char payload[] = "1";
    memset(buffer, 0xa5, sizeof(buffer));
    assert(heatpump_command(&installer, payload, buffer, log) == 0);
    for (byte b : buffer) assert(b == 0xa5);
    setInstallerCommandsAllowed(true);
    assert(heatpump_command(&installer, payload, buffer, log) == sizeof(panasonicSendQuery) && buffer[4] == 2);
    setInstallerCommandsAllowed(false);
    assert(heatpump_command(&installer, payload, buffer, log) == 0);
  }
  // payloads with one decimal: a sensor offset in a high nibble (0.5 K steps) and a whole byte in 0.1 steps
  { const cmdStruct offset = { "SetOffset", 0, CMD_DECIMAL(35, 4, -35, 35, 5, 5, 8) };
    const cmdStruct power = { "SetPower", 0, CMD_DECIMAL(51, 0, 1, 254, 1, 1, 1) };
    struct { const cmdStruct *command; const char *payload; int value; } cases[] = {
      { &offset, "-3.5", 0x10 }, { &offset, "-0.5", 0x70 }, { &offset, "0", 0x80 }, { &offset, "0.0", 0x80 },
      { &offset, "0.5", 0x90 }, { &offset, "2", 0xC0 }, { &offset, "3.5", 0xF0 }, { &offset, "-0", 0x80 },
      { &offset, "3.6", -1 }, { &offset, "4", -1 }, { &offset, "-4", -1 }, { &offset, "0.25", -1 },
      { &offset, "1.", -1 }, { &offset, ".5", -1 }, { &offset, "-", -1 }, { &offset, "-.5", -1 },
      { &offset, "", -1 }, { &offset, "abc", -1 }, { &offset, "1.5x", -1 }, { &offset, " 1", -1 },
      { &offset, "1,5", -1 }, { &offset, "0.5.0", -1 }, { &offset, "99999999", -1 },
      { &power, "0.1", 2 }, { &power, "4", 41 }, { &power, "4.2", 43 }, { &power, "25.4", 255 },
      { &power, "0", -1 }, { &power, "0.0", -1 }, { &power, "25.5", -1 }, { &power, "-1", -1 },
    };
    for (auto &c : cases) {
      byte buffer[256]; char log[256]; char payload[16];
      memset(buffer, 0xa5, sizeof(buffer)); strcpy(payload, c.payload);
      unsigned int length = heatpump_command(c.command, payload, buffer, log);
      if (c.value < 0) {
        if (length != 0) fail(c.command->name, c.payload, "length not 0");
        for (byte b : buffer) if (b != 0xa5) fail(c.command->name, c.payload, "buffer touched");
      } else if (length != sizeof(panasonicSendQuery) || buffer[c.command->address] != c.value) {
        printf("FAIL %s '%s': 0x%02X, expected 0x%02X\\n", c.command->name, c.payload, buffer[c.command->address], c.value);
        exit(1);
      }
    }
    assert(heatpump_command(&offset, nullptr, nullptr, (char *)memset(new char[256], 0, 256)) == 0);
  }
  // the log shows the value that is sent, not the payload
  { struct { const char *topic; const char *payload; const char *log; } cases[] = {
      { "SetDHWTemp", "48", "SetDHWTemp set to 48" }, { "SetDHWTemp", "256", "SetDHWTemp set to 0" },
      { "SetDHWTemp", "300", "SetDHWTemp set to 44" }, { "SetDHWTemp", "-5", "SetDHWTemp set to -5" },
      { "SetDHWTemp", "21.5", "SetDHWTemp set to 21" }, { "SetDHWTemp", "abc", "SetDHWTemp set to 0" },
      { "SetHeatpump", "1", "SetHeatpump set to 1" }, { "SetHeatpump", "2", "SetHeatpump set to 0" },
      { "SetQuietMode", "3", "SetQuietMode set to 3" }, { "SetQuietMode", "300", "SetQuietMode set to 4" },
      { "SetPowerfulMode", "2", "SetPowerfulMode set to 2" }, { "SetPowerfulMode", "21", "SetPowerfulMode set to 5" },
      { "SetBivalentMode", "2", "SetBivalentMode set to 2" }, { "SetBivalentMode", "3", "SetBivalentMode set to 0" },
      { "SetForceDefrost", "1", "SetForceDefrost set to 1" }, { "SetForceDefrost", "6", "SetForceDefrost set to 0" },
      { "SetRoomOperationMaxTime", "7620", "SetRoomOperationMaxTime set to 7620" },
      { "SetHeatToCoolTemp", "-127", "SetHeatToCoolTemp set to -127" },
      { "SetQuietModeSchedule", "1", "SetQuietModeSchedule set to 1" },
    };
    for (auto &c : cases) {
      byte buffer[256]; char log[256] = { 0 }; char payload[16];
      strcpy(payload, c.payload);
      if (heatpump_command(find(c.topic), payload, buffer, log) == 0 || strcmp(log, c.log) != 0) {
        printf("FAIL %s '%s': log '%s', expected '%s'\n", c.topic, c.payload, log, c.log);
        exit(1);
      }
    }
    const cmdStruct offset = { "SetOffset", 0, CMD_DECIMAL(35, 4, -35, 35, 5, 5, 8) };
    byte buffer[256]; char log[256] = { 0 }; char payload[] = "-3.5";
    assert(heatpump_command(&offset, payload, buffer, log) != 0 && strcmp(log, "SetOffset set to -3.5") == 0);
  }
  // decode_command() gives back the name and payload of every command
  { const char *payloads[] = { "1", "2", "3", "5", "-5", "21", "30", "60", "75" };
    unsigned int roundtrips = 0;
    for (unsigned int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
      if (commands[i].func == set_curves) continue;
      for (const char *text : payloads) {
        byte buffer[256]; char log[256]; char payload[16];
        strcpy(payload, text);
        if (heatpump_command(&commands[i], payload, buffer, log) == 0) continue;  // refused
        reports.clear();
        unsigned int count = decode_command(buffer, sizeof(panasonicSendQuery), report);
        // a payload outside the field is sent as something else, the log shows what
        const std::string sentAs = strrchr(log, ' ') + 1;
        const bool same = commands[i].type == CMD_FUNC ? count == 1 : sentAs == text;
        if (!same) continue;
        if (count != 1 || reports[0].first != commands[i].name || reports[0].second != text) {
          printf("FAIL decode %s '%s': %u reports, first %s = %s\n", commands[i].name, text, count,
                 count ? reports[0].first.c_str() : "-", count ? reports[0].second.c_str() : "-");
          exit(1);
        }
        roundtrips++;
      }
    }
    assert(roundtrips > 150);
    byte buffer[256];
    memcpy(buffer, panasonicSendQuery, sizeof(panasonicSendQuery));
    reports.clear();
    assert(decode_command(buffer, sizeof(panasonicSendQuery), report) == 0 && reports.empty());  // nothing set
    assert(decode_command(buffer, sizeof(panasonicSendQuery) - 1, report) == 0);  // not a command datagram
    // several settings in one datagram, with bits no command describes
    buffer[4] = 0x02 | 0x20;  // SetHeatpump 1, SetPump 1
    buffer[6] = 34 | 128;     // SetOperationMode 4, SetZones 1
    buffer[7] = 0x10 | 0x80;  // SetQuietMode 1, SetQuietModeSchedule 1
    buffer[42] = 128 + 55;    // SetDHWTemp 55
    buffer[60] = 0x12;        // unknown
    buffer[8] = 0x80 | 0x01;  // unknown bit, SetReset 1
    reports.clear();
    assert(decode_command(buffer, sizeof(panasonicSendQuery), report) == 10);
    const std::vector<std::pair<std::string, std::string>> expected = {
      { "SetHeatpump", "1" }, { "SetPump", "1" }, { "SetQuietMode", "1" }, { "SetDHWTemp", "55" }, { "SetReset", "1" },
      { "SetQuietModeSchedule", "1" }, { "SetOperationMode", "4" }, { "SetZones", "1" }, { "Byte8", "0x80" }, { "Byte60", "0x12" },
    };
    if (reports != expected) {
      for (auto &r : reports) printf("  %s = %s\n", r.first.c_str(), r.second.c_str());
      fail("decode", "combined", "unexpected reports");
    }
    // an operation mode byte that SetOperationMode can't send
    memcpy(buffer, panasonicSendQuery, sizeof(panasonicSendQuery));
    buffer[6] = 17;
    reports.clear();
    assert(decode_command(buffer, sizeof(panasonicSendQuery), report) == 1);
    assert(reports[0].first == "Byte6" && reports[0].second == "0x11");
    // curves as the JSON SetCurves takes
    struct { std::vector<std::pair<int, int>> bytes; const char *json; } curves[] = {
      { { { 75, 128 + 45 } }, "{\"zone1\":{\"heat\":{\"target\":{\"high\":45}}}}" },
      { { { 75, 128 + 45 }, { 76, 128 + 25 }, { 77, 128 - 15 }, { 78, 128 + 15 } },
        "{\"zone1\":{\"heat\":{\"target\":{\"high\":45,\"low\":25},\"outside\":{\"low\":-15,\"high\":15}}}}" },
      { { { 78, 128 + 15 }, { 86, 128 + 20 }, { 82, 128 + 12 }, { 93, 128 + 30 } },
        "{\"zone1\":{\"heat\":{\"outside\":{\"high\":15}},\"cool\":{\"target\":{\"high\":20}}},"
        "\"zone2\":{\"heat\":{\"outside\":{\"high\":12}},\"cool\":{\"outside\":{\"high\":30}}}}" },
    };
    for (auto &c : curves) {
      memcpy(buffer, panasonicSendQuery, sizeof(panasonicSendQuery));
      for (auto &b : c.bytes) buffer[b.first] = b.second;
      reports.clear();
      assert(decode_command(buffer, sizeof(panasonicSendQuery), report) == 1);
      if (reports[0].first != "SetCurves" || reports[0].second != c.json) fail("SetCurves", reports[0].second.c_str(), c.json);
    }
    // all 16 points fit the buffer
    memcpy(buffer, panasonicSendQuery, sizeof(panasonicSendQuery));
    for (int i = 75; i <= 93; i++) if (i < 83 || i > 85) buffer[i] = 128 - 100;
    reports.clear();
    assert(decode_command(buffer, sizeof(panasonicSendQuery), report) == 1);
    assert(reports[0].second.size() > 300 && reports[0].second.back() == '}' && reports[0].second[reports[0].second.size() - 2] == '}');
  }
  for (unsigned int i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    if (commands[i].type == CMD_FUNC) continue;
    byte buffer[256]; char log[256];
    memset(buffer, 0xa5, sizeof(buffer));
    unsigned int length = heatpump_command(&commands[i], nullptr, buffer, log);
    if (commands[i].type == CMD_STRICT || commands[i].type == CMD_TENTHS) { assert(length == 0); for (byte b : buffer) assert(b == 0xa5); }
    else assert(length == sizeof(panasonicSendQuery));
    assert(commands[i].address >= 4 && commands[i].address < sizeof(panasonicSendQuery));
    assert(commands[i].divisor > 0 && commands[i].step > 0);
  }
"""


def main():
    header = (ROOT / "HeishaMon/commands.h").read_text()
    source = (ROOT / "HeishaMon/commands.cpp").read_text()
    docs = (ROOT / "MQTT-Topics.md").read_text()
    names = re.findall(r'^  \{ "(Set\w+)", ', header[: header.index("struct optCmdStruct")], re.M)
    assert set(names) == set(RECORDED) | {"SetCurves"}, set(names) ^ (set(RECORDED) | {"SetCurves"})
    for name in names:
        assert f"| {name} |" in docs or f"`{name}`" in docs or name in docs, name

    template = re.search(r"byte panasonicSendQuery\[\].*?\};", source, re.S)[0]
    encoder = source[
        source.index("static bool installerCommandsAllowed") : source.index(
            "//start of optional pcb commands"
        )
    ]
    # SetCurves needs ArduinoJson, which the host build doesn't have
    encoder = (
        encoder[: encoder.index("unsigned int set_curves(")]
        + "unsigned int set_curves(char *, unsigned char *, char *) { return 0; }\n"
        + encoder[encoder.index("unsigned int set_zones(") :]
    )
    stubs = [
        f"unsigned int {name}({args}) {{ return 0; }}"
        for name, args in re.findall(
            r"unsigned int (set_\w+)\((char \*msg, char \*log_msg)\);", header
        )
    ]
    dispatcher = source[
        source.index("void send_heatpump_command(") : source.index(
            "// The optional PCB state is saved"
        )
    ]
    header = re.sub(r"^#include.*$", "", header, flags=re.M)

    checks = []
    for topic, (address, values) in RECORDED.items():
        for payload, value in zip(PAYLOADS, values):
            checks.append(
                CASE.format(topic=topic, payload=payload, address=address, value=-1 if value is None else value)
            )
    for topic, (address, accepted, refused) in CHECKED.items():
        for payload, value in accepted:
            checks.append(CASE.format(topic=topic, payload=payload, address=address, value=value))
        for payload in refused + REFUSED:
            checks.append(CASE.format(topic=topic, payload=payload, address=address, value=-1))

    program = SHIM + header + template + encoder + "\n".join(stubs) + dispatcher
    program += MAIN + "".join(checks) + GATE + "\n}\n"
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / "test.cpp"
        binary = Path(directory) / "test"
        cpp.write_text(program)
        subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print(f"PASS: {len(checks)} command cases for {len(names) - 1} commands (recorded bytes, boundaries, refused payloads)")


if __name__ == "__main__":
    main()
