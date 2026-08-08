/*
 * Host-side test harness for HeishaMon rulesets.
 *
 * Parses a rules.txt with the real firmware rules engine (src/rules/rules.cpp,
 * same rule_initialize() validation as on-device) and then drives a scripted
 * scenario against it: topic changes, virtual-clock timer expiry, and a
 * virtual wall clock for %hour/%minute. Every @Set… command the ruleset emits
 * is recorded and can be asserted on, so scenarios double as self-checking
 * regression tests (see the tests/ directory next to each example ruleset).
 *
 * Usage: harness <rules.txt> <scenario.txt>
 *        harness <rules.txt> parse        (parse/validate only)
 * Exit:  0 = parse ok, scenario ran, all expectations held
 *        2 = parse failed, 3 = expectation failed, 1 = usage/file errors
 *
 * Scenario ops (one per line, '#' starts a comment):
 *   boot                  fire System#Boot
 *   silent <Topic> <val>  set a topic value without firing its event
 *   set <Topic> <val>     set a topic value and fire its event if it changed
 *   adv <seconds>         advance the virtual clock, firing due timers
 *   time <hour> <minute>  set the virtual wall clock (it advances with adv)
 *   pump                  simulated pump accepts pending @Set… commands:
 *                         mapped read-back topics update and fire events
 *   expectcmd <Cmd> <val> assert the oldest unchecked emitted command
 *   expectnone            assert there are no unchecked emitted commands
 *   vars                  dump persistent # globals
 *   note <text>           print a marker line
 *
 * Timer semantics mirror src/common/timerqueue.cpp: re-arming a pending
 * timer replaces its delay, and setTimer(id, 0) CANCELS it (never fires).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include <string>
#include <map>
#include <deque>
#include <vector>
#include <algorithm>

#include "Arduino.h"   /* host shim */
#include "src/rules/rules.h"
#include "src/common/mem.h"
#include "src/common/strnicmp.h"

#include "valid_names.h"

/* ---------- host stubs required by the engine ---------- */

static int verbose = 0;

static void serial_printf(const char *fmt, ...) {
  if(!verbose) return;
  va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}
static void serial_println(const char *val) { if(verbose) fprintf(stderr, "%s\n", val); }
static void serial_flush(void) { }
struct serial_t Serial = { serial_printf, serial_println, serial_flush };

void *MMU_SEC_HEAP = NULL;
struct rule_options_t rule_options;

void _logprintln(const char *file, unsigned int line, char *msg) { if(verbose) fprintf(stderr, "[log] %s\n", msg); }
void _logprintf(const char *file, unsigned int line, char *fmt, ...) {
  if(!verbose) return;
  va_list ap; va_start(ap, fmt);
  fprintf(stderr, "[log] "); vfprintf(stderr, fmt, ap); fprintf(stderr, "\n");
  va_end(ap);
}
void _logprintln_P(const char *file, unsigned int line, const __FlashStringHelper *msg) { _logprintln(file, line, (char *)msg); }
void _logprintf_P(const char *file, unsigned int line, const __FlashStringHelper *fmt, ...) {
  if(!verbose) return;
  va_list ap; va_start(ap, (char *)fmt);
  fprintf(stderr, "[log] "); vfprintf(stderr, (const char *)fmt, ap); fprintf(stderr, "\n");
  va_end(ap);
}

static uint8_t parsing = 0;

static int gpio_state[64] = {0};
void digitalWrite(int pin, int state) {
  if(parsing == 0) printf("GPIO %d = %d\n", pin, state);
  if(pin >= 0 && pin < 64) gpio_state[pin] = state;
}
int digitalRead(int pin) { return (pin >= 0 && pin < 64) ? gpio_state[pin] : 0; }

/* ---------- virtual clock + timer queue (replaces common/timerqueue.cpp) ---------- */

static double vclock = 0;
struct PendingTimer { double at; int nr; };
static std::vector<PendingTimer> pending_timers;

/* virtual wall clock for %hour/%minute: seconds-of-day at vclock == wall_setat */
static double wall_base = 12 * 3600 + 30 * 60;
static double wall_setat = 0;

static int wall_seconds_of_day(void) {
  double s = wall_base + (vclock - wall_setat);
  return (int)fmod(fmod(s, 86400.0) + 86400.0, 86400.0);
}

void timerqueue_insert(int sec, int usec, int nr) {
  (void)usec;
  for(auto it = pending_timers.begin(); it != pending_timers.end(); ++it) {
    if(it->nr == nr) {
      if(sec <= 0) {
        /* like the firmware: re-arming with 0 removes the pending timer */
        pending_timers.erase(it);
        if(parsing == 0) printf("[t=%.0f] TIMER %d cancelled\n", vclock, nr);
      } else {
        it->at = vclock + sec;
        if(parsing == 0) printf("[t=%.0f] TIMER %d armed for %ds\n", vclock, nr, sec);
      }
      return;
    }
  }
  if(sec <= 0) return; /* like the firmware: arming a non-pending timer with 0 is a no-op */
  pending_timers.push_back({ vclock + sec, nr });
  if(parsing == 0) printf("[t=%.0f] TIMER %d armed for %ds\n", vclock, nr, sec);
}

/* ---------- rules state ---------- */

static struct rules_t **rules = NULL;
static uint8_t nrrules = 0;
static unsigned char *mempool = NULL;

struct Value {
  uint8_t type; /* VINTEGER, VFLOAT, VCHAR, VNULL */
  int i; float f; std::string s;
};

static std::map<std::string, Value> global_vars;                     /* # */
static std::map<rules_t *, std::map<std::string, Value>> local_vars; /* $ per block */
static std::map<std::string, std::string> topic_values;              /* @ read-back, key lowercased */

/* pump model: @Set command -> read-back topic it eventually updates.
 * Only identity-valued commands belong here (payload == new topic value);
 * commands with pump-side semantics (e.g. SetOperationMode) must be
 * simulated explicitly in the scenario with `set`. */
static std::map<std::string, std::string> set_to_read = {
  { "setz1heatrequesttemperature", "Z1_Heat_Request_Temp" },
  { "setz2heatrequesttemperature", "Z2_Heat_Request_Temp" },
  { "setz1coolrequesttemperature", "Z1_Cool_Request_Temp" },
  { "setheatpump",                 "Heatpump_State" },
  { "setmaxpumpduty",              "Max_Pump_Duty" },
  { "setquietmode",                "Quiet_Mode_Level" },
  { "setdhwtemp",                  "DHW_Target_Temp" },
};
struct PumpWrite { std::string cmd; std::string payload; };
static std::vector<PumpWrite> pending_pump_writes;

/* emitted commands awaiting expectcmd/expectnone checks */
static std::deque<PumpWrite> unchecked_cmds;
static int used_expect = 0;

static std::string lc(const std::string &s) {
  std::string r = s;
  std::transform(r.begin(), r.end(), r.begin(), ::tolower);
  return r;
}

static bool valid_at(const char *name, size_t len) {
  for(int i = 0; valid_at_names[i] != 0; i++) {
    if(strlen(valid_at_names[i]) == len &&
       strnicmp((char *)name, (char *)valid_at_names[i], len) == 0) return true;
  }
  return false;
}

/* ---------- engine callbacks (modeled on HeishaMon/rules.cpp) ---------- */

static int8_t is_variable_cb(char *text, uint16_t size) {
  uint16_t i = 1;
  if(size == strlen("ds18b20#2800000000000000") && strncmp(text, "ds18b20#", 8) == 0) {
    return 24;
  } else if(strncmp(text, "s0#", 3) == 0) {
    return size;
  } else if(text[0] == '$' || text[0] == '#' || text[0] == '@' || text[0] == '%' || text[0] == '?') {
    /* like the device glue: $/#/% names are alnum only (no underscores) */
    while(isalnum((unsigned char)text[i])) i++;
    if(text[0] == '%') {
      if(size == 5 && strncasecmp(&text[1], "hour", 4) == 0) return 5;
      if(size == 7 && strncasecmp(&text[1], "minute", 6) == 0) return 7;
      if(size == 6 && strncasecmp(&text[1], "month", 5) == 0) return 6;
      if(size == 4 && strncasecmp(&text[1], "day", 3) == 0) return 4;
    }
    if(text[0] == '@') {
      if(valid_at(&text[1], size-1)) return (int8_t)size;
      fprintf(stderr, "!!! unknown @name: %.*s\n", size, text);
      return -1;
    }
    if(text[0] == '?') return -1; /* OpenTherm not simulated */
    return i;
  }
  return -1;
}

static int8_t is_event_cb(char *text, uint16_t size) {
  if(text[0] == '@') {
    if(valid_at(&text[1], size-1)) return (int8_t)size;
    fprintf(stderr, "!!! unknown @event: %.*s\n", size, text);
    return -1;
  }
  if(size == strlen("ds18b20#2800000000000000") && strncmp(text, "ds18b20#", 8) == 0) return 24;
  if(isalpha((unsigned char)text[0]) || text[0] == '_') {
    uint16_t i = 0;
    while(i < size && (isalnum((unsigned char)text[i]) || text[i] == '_' || text[i] == '#')) i++;
    if(i == size) return size;
  }
  return -1;
}

static int8_t event_cb(struct rules_t *obj, char *name) {
  int8_t nr = rule_by_name(rules, nrrules, name);
  if(nr == -1) {
    fprintf(stderr, "!!! rule block '%s' not found\n", name);
    return -1;
  }
  obj->ctx.go = rules[nr];
  rules[nr]->ctx.ret = obj;
  return 1;
}

static void rule_done_cb(struct rules_t *obj) { }

static int8_t check_is_number(const char *str) {
  size_t len = strlen(str), nrdot = 0;
  if(len == 0) return -1;
  if(str[0] != '-' && !isdigit((unsigned char)str[0])) return -1;
  for(size_t pos = 1; pos < len; pos++) {
    if(str[pos] == '.') { if(++nrdot > 1) return -1; }
    else if(!isdigit((unsigned char)str[pos])) return -1;
  }
  return 0;
}

static void push_topic_str(const char *str) {
  if(strlen(str) == 0) { rules_pushnil(); return; }
  if(check_is_number(str) == 0) {
    float var = atof(str), nr = 0;
    if(modff(var, &nr) == 0) rules_pushinteger((int)var);
    else rules_pushfloat(var);
  } else {
    rules_pushstring((char *)str);
  }
}

static int8_t vm_value_get(struct rules_t *obj) {
  if(rules_gettop() < 1 || rules_type(-1) != VCHAR) return -1;
  const char *key = rules_tostring(-1);

  if(key[0] == '%') {
    int sod = wall_seconds_of_day();
    if(strcasecmp(&key[1], "hour") == 0)   { rules_pushinteger(sod / 3600); return 0; }
    if(strcasecmp(&key[1], "minute") == 0) { rules_pushinteger((sod / 60) % 60); return 0; }
    if(strcasecmp(&key[1], "month") == 0)  { rules_pushinteger(1); return 0; }
    if(strcasecmp(&key[1], "day") == 0)    { rules_pushinteger(1); return 0; }
    rules_pushnil(); return 0;
  }
  if(key[0] == '@') {
    auto it = topic_values.find(lc(&key[1]));
    if(it == topic_values.end()) {
      if(parsing == 0) fprintf(stderr, "!!! read of unset topic %s -> NULL\n", key);
      rules_pushnil();
      return 0;
    }
    push_topic_str(it->second.c_str());
    return 0;
  }
  std::map<std::string, Value> *table = NULL;
  if(key[0] == '$') table = &local_vars[obj];
  else if(key[0] == '#') table = &global_vars;
  if(table == NULL) { rules_pushnil(); return 0; }

  auto it = table->find(key);
  if(it == table->end()) { rules_pushnil(); return 0; }
  switch(it->second.type) {
    case VINTEGER: rules_pushinteger(it->second.i); break;
    case VFLOAT:   rules_pushfloat(it->second.f); break;
    case VCHAR:    rules_pushstring((char *)it->second.s.c_str()); break;
    default:       rules_pushnil(); break;
  }
  return 0;
}

static int8_t vm_value_set(struct rules_t *obj) {
  if(rules_gettop() < 2) return -1;
  uint8_t type = rules_type(-1);
  if(rules_type(-2) != VCHAR ||
     (type != VINTEGER && type != VFLOAT && type != VNULL && type != VCHAR)) return -1;

  const char *key = rules_tostring(-2);

  if(key[0] == '@') {
    char payload[128] = { 0 };
    switch(type) {
      case VCHAR:    snprintf(payload, sizeof(payload), "%s", rules_tostring(-1)); break;
      case VINTEGER: snprintf(payload, sizeof(payload), "%d", rules_tointeger(-1)); break;
      case VFLOAT:   snprintf(payload, sizeof(payload), "%g", rules_tofloat(-1)); break;
      case VNULL:    snprintf(payload, sizeof(payload), "NULL"); break;
    }
    if(parsing == 0) {
      printf("[t=%.0f] CMD %s = %s\n", vclock, &key[1], payload);
      pending_pump_writes.push_back({ lc(&key[1]), payload });
      unchecked_cmds.push_back({ &key[1], payload });
    }
    return 0;
  }

  std::map<std::string, Value> *table = NULL;
  if(key[0] == '$') table = &local_vars[obj];
  else if(key[0] == '#') table = &global_vars;
  if(table == NULL) return 0;

  Value v; v.type = type; v.i = 0; v.f = 0;
  switch(type) {
    case VINTEGER: v.i = rules_tointeger(-1); break;
    case VFLOAT:   v.f = rules_tofloat(-1); break;
    case VCHAR:    v.s = rules_tostring(-1); break;
  }
  (*table)[key] = v;
  return 0;
}

/* ---------- parse + run ---------- */

static int parse_rules(const char *path) {
  FILE *f = fopen(path, "rb");
  if(f == NULL) { fprintf(stderr, "cannot open %s\n", path); return -1; }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);

  memset(mempool, 0, MEMPOOL_SIZE);
  unsigned int txtoffset = alignedbuffer(MEMPOOL_SIZE - len - 5);
  if(fread(&mempool[txtoffset], 1, len, f) != (size_t)len) { fclose(f); return -1; }
  fclose(f);

  struct pbuf mem; memset(&mem, 0, sizeof(mem));
  struct pbuf input; memset(&input, 0, sizeof(input));
  mem.payload = mempool;
  mem.len = 0;
  mem.tot_len = MEMPOOL_SIZE;
  input.payload = &mempool[txtoffset];
  input.len = txtoffset;
  input.tot_len = len;

  parsing = 1;
  int ret = 0;
  while((ret = rule_initialize(&input, &rules, &nrrules, &mem, NULL)) == 0) {
    input.payload = &mempool[input.len];
  }
  parsing = 0;

  pending_timers.clear();
  global_vars.clear();
  local_vars.clear();

  if(ret == -1) return -1;
  printf("PARSE OK (%d blocks, %d/%d bytes mempool)\n", nrrules, mem.len, mem.tot_len);
  return 0;
}

static void run_block(int8_t nr, const char *label) {
  if(nr < 0) return;
  printf("[t=%.0f] FIRE %s\n", vclock, label);
  int ret = rule_run(rules[nr], 0);
  if(ret == 0) local_vars.clear(); /* locals do not persist across firings */
}

static void fire_event(const char *name_with_sigil) {
  int8_t nr = rule_by_name(rules, nrrules, (char *)name_with_sigil);
  if(nr > -1) run_block(nr, name_with_sigil);
}

static void fire_timer(int nr) {
  char name[24];
  snprintf(name, sizeof(name), "timer=%d", nr);
  int8_t r = rule_by_name(rules, nrrules, name);
  if(r > -1) run_block(r, name);
}

static void set_topic(const std::string &topic, const std::string &value, bool fire) {
  std::string k = lc(topic);
  auto it = topic_values.find(k);
  bool changed = (it == topic_values.end() || it->second != value);
  topic_values[k] = value;
  if(fire && changed) {
    std::string ev = "@" + topic;
    printf("[t=%.0f] TOPIC %s -> %s\n", vclock, topic.c_str(), value.c_str());
    fire_event(ev.c_str());
  }
}

static void advance(double sec) {
  double target = vclock + sec;
  while(true) {
    /* earliest due timer within the window */
    int best = -1;
    for(size_t i = 0; i < pending_timers.size(); i++) {
      if(pending_timers[i].at <= target &&
         (best == -1 || pending_timers[i].at < pending_timers[best].at)) best = (int)i;
    }
    if(best == -1) break;
    PendingTimer t = pending_timers[best];
    pending_timers.erase(pending_timers.begin() + best);
    vclock = t.at;
    printf("[t=%.0f] TIMER %d fired\n", vclock, t.nr);
    fire_timer(t.nr);
  }
  vclock = target;
}

static void pump_apply(void) {
  /* the simulated heat pump accepts all pending commands; HeishaMon polls the
   * new values back, which fires the matching read-topic events */
  std::vector<PumpWrite> writes;
  writes.swap(pending_pump_writes);
  for(auto &w : writes) {
    auto it = set_to_read.find(w.cmd);
    if(it != set_to_read.end()) {
      set_topic(it->second, w.payload, true);
    }
  }
}

static void dump_vars(void) {
  for(auto &kv : global_vars) {
    switch(kv.second.type) {
      case VINTEGER: printf("VAR %s = %d\n", kv.first.c_str(), kv.second.i); break;
      case VFLOAT:   printf("VAR %s = %g\n", kv.first.c_str(), kv.second.f); break;
      case VCHAR:    printf("VAR %s = %s\n", kv.first.c_str(), kv.second.s.c_str()); break;
      default:       printf("VAR %s = NULL\n", kv.first.c_str()); break;
    }
  }
}

static int fail(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  printf("[t=%.0f] FAIL ", vclock); vprintf(fmt, ap); printf("\n");
  va_end(ap);
  return 3;
}

int main(int argc, char **argv) {
  if(argc < 3) {
    fprintf(stderr, "usage: %s <rules.txt> <scenario.txt|parse>\n", argv[0]);
    return 1;
  }
  verbose = getenv("HARNESS_VERBOSE") != NULL;

  mempool = (unsigned char *)malloc(MEMPOOL_SIZE);
  MMU_SEC_HEAP = malloc(MEMPOOL_SIZE);

  memset(&rule_options, 0, sizeof(struct rule_options_t));
  rule_options.is_variable_cb = is_variable_cb;
  rule_options.is_event_cb = is_event_cb;
  rule_options.done_cb = rule_done_cb;
  rule_options.vm_value_set = vm_value_set;
  rule_options.vm_value_get = vm_value_get;
  rule_options.event_cb = event_cb;

  if(parse_rules(argv[1]) != 0) {
    printf("PARSE FAILED\n");
    return 2;
  }
  if(strcmp(argv[2], "parse") == 0) {
    return 0;
  }

  FILE *sc = fopen(argv[2], "r");
  if(sc == NULL) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }

  char line[256];
  while(fgets(line, sizeof(line), sc) != NULL) {
    char *p = line;
    while(*p == ' ' || *p == '\t') p++;
    p[strcspn(p, "\r\n")] = 0;
    if(*p == 0 || *p == '#') continue;

    char cmd[32] = {0}, a1[128] = {0}, a2[128] = {0};
    sscanf(p, "%31s %127s %127s", cmd, a1, a2);

    if(strcmp(cmd, "boot") == 0) {
      fire_event("System#Boot");
    } else if(strcmp(cmd, "set") == 0) {
      set_topic(a1, a2, true);
    } else if(strcmp(cmd, "silent") == 0) {
      set_topic(a1, a2, false);
    } else if(strcmp(cmd, "adv") == 0) {
      advance(atof(a1));
    } else if(strcmp(cmd, "time") == 0) {
      wall_base = atoi(a1) * 3600 + atoi(a2) * 60;
      wall_setat = vclock;
      printf("[t=%.0f] WALLCLOCK %02d:%02d\n", vclock, atoi(a1), atoi(a2));
    } else if(strcmp(cmd, "pump") == 0) {
      pump_apply();
    } else if(strcmp(cmd, "expectcmd") == 0) {
      used_expect = 1;
      if(unchecked_cmds.empty()) {
        return fail("expected CMD %s = %s but no command was emitted", a1, a2);
      }
      PumpWrite got = unchecked_cmds.front();
      unchecked_cmds.pop_front();
      if(strcasecmp(got.cmd.c_str(), a1) != 0 || got.payload != a2) {
        return fail("expected CMD %s = %s but got CMD %s = %s",
                    a1, a2, got.cmd.c_str(), got.payload.c_str());
      }
      printf("[t=%.0f] OK CMD %s = %s\n", vclock, a1, a2);
    } else if(strcmp(cmd, "expectnone") == 0) {
      used_expect = 1;
      if(!unchecked_cmds.empty()) {
        PumpWrite got = unchecked_cmds.front();
        return fail("expected no commands but got CMD %s = %s",
                    got.cmd.c_str(), got.payload.c_str());
      }
      printf("[t=%.0f] OK no commands\n", vclock);
    } else if(strcmp(cmd, "vars") == 0) {
      dump_vars();
    } else if(strcmp(cmd, "note") == 0) {
      printf("[t=%.0f] --- %s %s\n", vclock, a1, a2);
    } else {
      fprintf(stderr, "unknown scenario op: %s\n", cmd);
      return 1;
    }
  }
  fclose(sc);

  if(used_expect && !unchecked_cmds.empty()) {
    PumpWrite got = unchecked_cmds.front();
    return fail("scenario ended with unchecked CMD %s = %s (add expectcmd/expectnone)",
                got.cmd.c_str(), got.payload.c_str());
  }
  printf("DONE t=%.0f\n", vclock);
  return 0;
}
