/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include <Arduino.h>
#include <LittleFS.h>

#include "src/common/mem.h"
#include "src/common/stricmp.h"
#include "src/common/strnicmp.h"
#include "src/common/log.h"
#include "src/common/uint32float.h"
#include "src/common/timerqueue.h"
#include "src/common/progmem.h"
#include "src/rules/rules.h"

#include "dallas.h"
#include "webfunctions.h"
#include "decode.h"
#include "HeishaOT.h"
#include "commands.h"
#include "rules.h"

#define MAXCOMMANDSINBUFFER 10
#define OPTDATASIZE 20

bool send_command(byte* command, int length);

extern int dallasDevicecount;
extern dallasDataStruct *actDallasData;
extern settingsStruct heishamonSettings;
extern char actData[DATASIZE];
extern char actOptData[OPTDATASIZE];
extern char actDataExtra[DATASIZE];
extern String openTherm[2];
static uint8_t parsing = 0;

static struct rules_t **rules = NULL;
uint8_t nrrules = 0;

struct rule_options_t rule_options;

typedef struct rule_timer_t {
  uint32_t first;
  uint32_t second;
} __attribute__((aligned(4))) rule_timer_t;

static struct rule_timer_t timestamp;

typedef struct array_t {
  const char *key;
  union {
    int i;
    float f;
    void *n;
    const char *s;
  } val;
  uint8_t type;
} array_t;

typedef struct varstack_t {
  struct array_t *array;
  uint16_t nr;
} varstack_t;

static struct varstack_t global_varstack = { .array = NULL, .nr = 0 };

#if defined(ESP8266)
unsigned char *mempool = (unsigned char *)MEMPOOL_ADDRESS;
#elif defined(ESP32)
unsigned char *mempool; //malloc in runtime
#endif
unsigned int memptr = 0;

// static int readRuleFromFS(int i) {
  // char fname[24];
  // memset(&fname, 0, sizeof(fname));
  // sprintf_P((char *)&fname, PSTR("/rule%d.bc"), i);
  // File f = LittleFS.open(fname, "r");
  // if(!f) {
    // logprintf_P(F("failed to open file: %s"), fname);
    // return -1;
  // }
  // FREE(rules[i]->ast.buffer);
  // if((rules[i]->ast.buffer = (unsigned char *)MALLOC(rules[i]->ast.bufsize)) == NULL) {
    // OUT_OF_MEMORY
  // }
  // memset(rules[i]->ast.buffer, 0, rules[i]->ast.bufsize);
  // f.readBytes((char *)rules[i]->ast.buffer, rules[i]->ast.bufsize);
  // f.close();
  // return 0;
// }

// static int writeRuleToFS(int i) {
  // char fname[24];
  // memset(&fname, 0, sizeof(fname));
  // sprintf_P((char *)&fname, PSTR("/rule%d.bc"), i);
  // File f = LittleFS.open(fname, "w");
  // if(!f) {
    // logprintf_P(F("failed to open file: %s"), fname);
    // return -1;
  // }
  // f.write((char *)rules[i]->ast.buffer, rules[i]->ast.bufsize);
  // f.close();
  // return 0;
// }

static int8_t is_variable(char *text, uint16_t size) {
  uint16_t i = 1, x = 0, match = 0;

  if(size == strlen_P(PSTR("ds18b20#2800000000000000")) && strncmp_P(text, PSTR("ds18b20#"), 8) == 0) {
    return 24;
  } else if(text[0] == '$' || text[0] == '#' || text[0] == '@' || text[0] == '%' || text[0] == '?') {
    while(isalnum(text[i])) {
      i++;
    }

    if(text[0] == '%') {
      if(size == 5 && strnicmp(&text[1], "hour", 4) == 0) {
        return 5;
      }
      if(size == 7 && strnicmp(&text[1], "minute", 6) == 0) {
        return 7;
      }
      if(size == 6 && strnicmp(&text[1], "month", 5) == 0) {
        return 6;
      }
      if(size == 4 && strnicmp(&text[1], "day", 3) == 0) {
        return 4;
      }
    }

    if(text[0] == '@') {
      int nrcommands = sizeof(commands)/sizeof(commands[0]);
      for(x=0;x<nrcommands;x++) {
        cmdStruct cmd;
        memcpy_P(&cmd, &commands[x], sizeof(cmd));
        size_t len = strlen(cmd.name);
        if(size-1 == len && strnicmp(&text[1], cmd.name, len) == 0) {
          i = len+1;
          match = 1;
          break;
        }
      }

      int nroptcommands = sizeof(optionalCommands)/sizeof(optionalCommands[0]);
      for(x=0;x<nroptcommands;x++) {
        optCmdStruct cmd;
        memcpy_P(&cmd, &optionalCommands[x], sizeof(cmd));
        size_t len = strlen(cmd.name);
        if(size-1 == len && strnicmp(&text[1], cmd.name, len) == 0) {
          i = len+1;
          match = 1;
          break;
        }
      }
      if(match == 0) {
        int nrtopics = sizeof(topics)/sizeof(topics[0]);
        for(x=0;x<nrtopics;x++) {
          char cpy[MAX_TOPIC_LEN];
          memcpy_P(&cpy, topics[x], MAX_TOPIC_LEN);
          size_t len = strlen(cpy);
          if(size-1 == len && strnicmp(&text[1], cpy, len) == 0) {
            i = len+1;
            match = 1;
            break;
          }
        }
      }
      if(match == 0) {
        int nrtopics = sizeof(optTopics)/sizeof(optTopics[0]);
        for(x=0;x<nrtopics;x++) {
          char cpy[MAX_TOPIC_LEN];
          memcpy_P(&cpy, optTopics[x], MAX_TOPIC_LEN);
          size_t len = strlen(cpy);
          if(size-1 == len && strnicmp(&text[1], cpy, len) == 0) {
            i = len+1;
            match = 1;
            break;
          }
        }
      }
      if(match == 0) {
        int nrtopics = sizeof(xtopics)/sizeof(xtopics[0]);
        for(x=0;x<nrtopics;x++) {
          char cpy[MAX_TOPIC_LEN];
          memcpy_P(&cpy, xtopics[x], MAX_TOPIC_LEN);
          size_t len = strlen(cpy);
          if(size-1 == len && strnicmp(&text[1], cpy, len) == 0) {
            i = len+1;
            match = 1;
            break;
          }
        }
      }
      if(match == 0) {
        return -1;
      }
    }
    if(text[0] == '?') {
      int x = 0;
      while(heishaOTDataStruct[x].name != NULL) {
        size_t len = strlen(heishaOTDataStruct[x].name);
        if(size-1 == len && strnicmp(&text[1], heishaOTDataStruct[x].name, len) == 0) {
          i = len+1;
          match = 1;
          break;
        }
        x++;
      }
      if(match == 0) {
        logprintf_P(F("err: %s %d"), __FUNCTION__, __LINE__);
        return -1;
      }
    }

    return i;
  }
  return -1;
}

static int8_t is_event(char *text, uint16_t size) {
  int i = 1, x = 0, match = 0;
  if(text[0] == '@') {
    int nrcommands = sizeof(commands)/sizeof(commands[0]);
    for(x=0;x<nrcommands;x++) {
      cmdStruct cmd;
      memcpy_P(&cmd, &commands[x], sizeof(cmd));
      size_t len = strlen(cmd.name);
      if(size-1 == len && strnicmp(&text[1], cmd.name, len) == 0) {
        i = len+1;
        match = 1;
        break;
      }
    }

    if(match == 0) {
      int nroptcommands = sizeof(optionalCommands)/sizeof(optionalCommands[0]);
      for(x=0;x<nroptcommands;x++) {
        optCmdStruct cmd;
        memcpy_P(&cmd, &optionalCommands[x], sizeof(cmd));
        size_t len = strlen(cmd.name);
        if(size-1 == len && strnicmp(&text[1], cmd.name, len) == 0) {
          i = len+1;
          match = 1;
          break;
        }
      }
    }
    if(match == 0) {
      int nrtopics = sizeof(topics)/sizeof(topics[0]);
      for(x=0;x<nrtopics;x++) {
        size_t len = strlen_P(topics[x]);
        char cpy[len];
        memcpy_P(&cpy, &topics[x], len);
        if(size-1 == len && strnicmp(&text[1], cpy, len) == 0) {
          i = len+1;
          match = 1;
          break;
        }
      }
    }
    if(match == 0) {
      int nrtopics = sizeof(optTopics)/sizeof(optTopics[0]);
      for(x=0;x<nrtopics;x++) {
        size_t len = strlen_P(optTopics[x]);
        char cpy[len];
        memcpy_P(&cpy, &optTopics[x], len);
        if(size-1 == len && strnicmp(&text[1], cpy, len) == 0) {
          i = len+1;
          match = 1;
          break;
        }
      }
    }
    if(match == 0) {
      int nrtopics = sizeof(xtopics)/sizeof(xtopics[0]);
      for(x=0;x<nrtopics;x++) {
        size_t len = strlen_P(xtopics[x]);
        char cpy[len];
        memcpy_P(&cpy, &xtopics[x], len);
        if(size-1 == len && strnicmp(&text[1], cpy, len) == 0) {
          i = len+1;
          match = 1;
          break;
        }
      }
    }
    if(match == 0) {
      return -1;
    }

    return i;
  }

  if(text[0] == '?') {
    int x = 0;
    while(heishaOTDataStruct[x].name != NULL) {
      size_t len = strlen(heishaOTDataStruct[x].name);
      if(size-1 == len && strnicmp(&text[1], heishaOTDataStruct[x].name, len) == 0) {
        i = len+1;
        match = 1;
        break;
      }
      x++;
    }
    if(match == 0) {
      return -1;
    }
    return i;
  }

  if(size == strlen_P(PSTR("ds18b20#2800000000000000")) && strncmp_P((const char *)text, PSTR("ds18b20#"), 8) == 0) {
    return 24;
  }

  uint8_t nr = rule_by_name(rules, nrrules, text);
  if(nr > 0) {
    return size;
  }

  return -1;
}

static void rule_done_cb(struct rules_t *obj) {
  return;
}

static int8_t event_cb(struct rules_t *obj, char *name) {
  int8_t nr = rule_by_name(rules, nrrules, name);
  if(nr == -1) {
    char msg[100];
    sprintf_P((char *)&msg, PSTR("Rule block '%s' not found"), name);
    log_message(name);
    return -1;
  }

  obj->ctx.go = rules[nr];
  rules[nr]->ctx.ret = obj;

  return 1;
}

static int8_t vm_value_get(struct rules_t *obj) {
  int16_t x = 0;

  if(rules_gettop(obj) < 1) {
    return -1;
  }
  if(rules_type(obj, -1) != VCHAR) {
    return -1;
  }

  const char *key = rules_tostring(obj, -1);

  if(key[0] == '?') {
    int x = 0;
    while(heishaOTDataStruct[x].name != NULL) {
      if(heishaOTDataStruct[x].rw >= 2 &&
         stricmp((char *)&key[1], heishaOTDataStruct[x].name) == 0) {
        if(heishaOTDataStruct[x].type == TBOOL) {
          rules_pushinteger(obj, (int)heishaOTDataStruct[x].value.b);
          return 0;
        }
        if(heishaOTDataStruct[x].type == TFLOAT) {
          rules_pushfloat(obj, heishaOTDataStruct[x].value.f);
          return 0;
        }
        break;
      }
      x++;
    }
    logprintf_P(F("err: %s %d"), __FUNCTION__, __LINE__);
  } else if(key[0] == '%') {
    time_t now = time(NULL);
    struct tm *tm_struct = localtime(&now);
    if(stricmp((char *)&key[1], "hour") == 0) {
      rules_pushinteger(obj, (int)tm_struct->tm_hour);
      return 0;
    } else if(stricmp((char *)&key[1], "minute") == 0) {
      rules_pushinteger(obj, (int)tm_struct->tm_min);
      return 0;
    } else if(stricmp((char *)&key[1], "month") == 0) {
      rules_pushinteger(obj, (int)tm_struct->tm_mon);
      return 0;
    } else if(stricmp((char *)&key[1], "day") == 0) {
      rules_pushinteger(obj, (int)tm_struct->tm_wday+1);
      return 0;
    }
  } else if(strnicmp((const char *)key, _F("ds18b20#"), 8) == 0) {
    uint8_t i = 0;
    for(i=0;i<dallasDevicecount;i++) {
      if(strncmp(actDallasData[i].address, (const char *)&key[8], 16) == 0) {
        rules_pushfloat(obj, actDallasData[i].temperature);
        return 0;
      }
    }
    rules_pushnil(obj);
    return 0;
  } else if(key[0] == '@') {
    uint16_t i = 0;
    for(i=0;i<NUMBER_OF_TOPICS;i++) {
      char cpy[MAX_TOPIC_LEN];
      memcpy_P(&cpy, topics[i], MAX_TOPIC_LEN);
      if(stricmp(cpy, (char *)&key[1]) == 0) {
        String dataValue = actData[0] == '\0' ? "" : getDataValue(actData, i);
        char *str = (char *)dataValue.c_str();
        if(strlen(str) == 0) {
          rules_pushnil(obj);
        } else {
          float var = atof(str);
          float nr = 0;

          if(modff(var, &nr) == 0) {
            rules_pushinteger(obj, (int)var);
            return 0;
          } else {
            rules_pushfloat(obj, var);
            return 0;
          }
        }
      }
    }
    for(i=0;i<NUMBER_OF_OPT_TOPICS;i++) {
      char cpy[MAX_TOPIC_LEN];
      memcpy_P(&cpy, topics[i], MAX_TOPIC_LEN);
      if(stricmp(cpy, (char *)&key[1]) == 0) {
        String dataValue = actOptData[0] == '\0' ? "" : getOptDataValue(actOptData, i);
        char *str = (char *)dataValue.c_str();
        if(strlen(str) == 0) {
          rules_pushnil(obj);
        } else {
          float var = atof(str);
          float nr = 0;

          if(modff(var, &nr) == 0) {
            rules_pushinteger(obj, (int)var);
            return 0;
          } else {
            rules_pushfloat(obj, var);
            return 0;
          }
        }
      }
    }
    for(i=0;i<NUMBER_OF_TOPICS_EXTRA;i++) {
      char cpy[MAX_TOPIC_LEN];
      memcpy_P(&cpy, xtopics[i], MAX_TOPIC_LEN);
      if(stricmp(cpy, (char *)&key[1]) == 0) {
        String dataValue = actDataExtra[0] == '\0' ? "" : getDataValueExtra(actDataExtra, i);
        char *str = (char *)dataValue.c_str();
        if(strlen(str) == 0) {
          rules_pushnil(obj);
        } else {
          float var = atof(str);
          float nr = 0;

          if(modff(var, &nr) == 0) {
            rules_pushinteger(obj, (int)var);
            return 0;
          } else {
            rules_pushfloat(obj, var);
            return 0;
          }
        }
      }
    }
  } else {
    struct varstack_t *table = NULL;
    struct array_t *array = NULL;
    if(key[0] == '$') {
      table = (struct varstack_t *)obj->userdata;
    } else if(key[0] == '#') {
      table = &global_varstack;
    }
    if(table == NULL) {
      rules_pushnil(obj);
    } else {
      for(x=0;x<table->nr;x++) {
        if(strcmp(table->array[x].key, key) == 0) {
          array = &table->array[x];
          break;
        }
      }
      if(array == NULL) {
        rules_pushnil(obj);
      } else {
        switch(array->type) {
          case VINTEGER: {
            rules_pushinteger(obj, array->val.i);
          } break;
          case VFLOAT: {
            rules_pushfloat(obj, array->val.f);
          } break;
          case VCHAR: {
            rules_pushstring(obj, (char *)array->val.s);
          } break;
          case VNULL: {
            rules_pushnil(obj);
          } break;
        }
      }
    }
  }

  return 0;
}

static int8_t vm_value_set(struct rules_t *obj) {
  struct varstack_t *table = NULL;
  uint16_t x = 0;
  uint8_t type = 0;

  if(rules_gettop(obj) < 2) {
    return -1;
  }
  type = rules_type(obj, -1);

  if(rules_type(obj, -2) != VCHAR
    || (type != VINTEGER && type != VFLOAT && type != VNULL && type != VCHAR)) {
    return -1;
  }

  const char *key = rules_tostring(obj, -2);

  if(key[0] == '@') {
    char *payload = NULL;
    unsigned int len = 0;

    switch(type) {
      case VCHAR: {
        len = snprintf_P(NULL, 0, PSTR("%s"), rules_tostring(obj, -1));
        if((payload = (char *)MALLOC(len+1)) == NULL) {
          OUT_OF_MEMORY
        }
        snprintf_P(payload, len+1, PSTR("%s"), rules_tostring(obj, -1));
      } break;
      case VINTEGER: {
        int val = rules_tointeger(obj, -1);
        len = snprintf_P(NULL, 0, PSTR("%d"), val);
        if((payload = (char *)MALLOC(len+1)) == NULL) {
          OUT_OF_MEMORY
        }
        snprintf_P(payload, len+1, PSTR("%d"), val);
      } break;
      case VFLOAT: {
        float val = rules_tofloat(obj, -1);
        len = snprintf_P(NULL, 0, PSTR("%g"), val);
        if((payload = (char *)MALLOC(len+1)) == NULL) {
          OUT_OF_MEMORY
        }
        snprintf_P(payload, len+1, PSTR("%g"), val);
      } break;
    }

    if(parsing == 0 && !heishamonSettings.listenonly) {
      unsigned char cmd[256] = { 0 };
      char log_msg[256] = { 0 };

      for(uint8_t x = 0; x < sizeof(commands) / sizeof(commands[0]); x++) {
        cmdStruct tmp;
        memcpy_P(&tmp, &commands[x], sizeof(tmp));
        if(stricmp((char *)&key[1], tmp.name) == 0) {
          uint16_t len = tmp.func(payload, cmd, log_msg);
          log_message(log_msg);
          send_command(cmd, len);
          break;
        }
      }

      memset(&cmd, 256, 0);
      memset(&log_msg, 256, 0);

      if(heishamonSettings.optionalPCB) {
        //optional commands
        for(uint8_t x = 0; x < sizeof(optionalCommands) / sizeof(optionalCommands[0]); x++) {
          optCmdStruct tmp;
          memcpy_P(&tmp, &optionalCommands[x], sizeof(tmp));
          if(stricmp((char *)&key[1], tmp.name) == 0) {
            uint16_t len = tmp.func(payload, log_msg);
            log_message(log_msg);
            break;
          }
        }
      }
    }
    FREE(payload);
  } else if(key[0] == '?') {
    int x = 0;
    while(heishaOTDataStruct[x].name != NULL) {
      if(heishaOTDataStruct[x].rw <= 2 && stricmp((char *)&key[1], heishaOTDataStruct[x].name) == 0) {
        if(heishaOTDataStruct[x].type == TBOOL) {
          switch(type) {
            case VINTEGER: {
              heishaOTDataStruct[x].value.b = (bool)rules_tointeger(obj, -1);
            } break;
            case VFLOAT: {
              heishaOTDataStruct[x].value.b = (bool)rules_tofloat(obj, -1);
            } break;
          }
        } else if(heishaOTDataStruct[x].type == TFLOAT) {
          switch(type) {
            case VINTEGER: {
              heishaOTDataStruct[x].value.f = (float)rules_tointeger(obj, -1);
            } break;
            case VFLOAT: {
              heishaOTDataStruct[x].value.f = rules_tointeger(obj, -1);
            } break;
          }
        }
        break;
      }
      x++;
    }
  } else {
    if(key[0] == '$') {
      table = (struct varstack_t *)obj->userdata;
      if(table == NULL) {
        if((table = (struct varstack_t *)MALLOC(sizeof(struct varstack_t))) == NULL) {
          OUT_OF_MEMORY
        }
        memset(table, 0, sizeof(struct varstack_t));
        obj->userdata = table;
      }
    } else if(key[0] == '#') {
      table = (struct varstack_t *)&global_varstack;
    }

    struct array_t *array = NULL;
    for(x=0;x<table->nr;x++) {
      if(strcmp(table->array[x].key, key) == 0) {
        array = &table->array[x];
        break;
      }
    }

    if(array == NULL) {
      if((table->array = (struct array_t *)REALLOC(table->array, sizeof(struct array_t)*(table->nr+1))) == NULL) {
        OUT_OF_MEMORY
      }
      array = &table->array[table->nr];
      memset(array, 0, sizeof(struct array_t));
      table->nr++;
      rules_ref(key);
    }

    array->key = key;

    switch(type) {
      case VINTEGER: {
        if(array->type == VCHAR && array->val.s != NULL) {
          rules_unref(array->val.s);
        }
        array->val.i = rules_tointeger(obj, -1);
        array->type = VINTEGER;
      } break;
      case VFLOAT: {
        if(array->type == VCHAR && array->val.s != NULL) {
          rules_unref(array->val.s);
        }
        array->val.f = rules_tofloat(obj, -1);
        array->type = VFLOAT;
      } break;
      case VCHAR: {
        uint8_t doref = 1;
        if(array->type == VCHAR && array->val.s != NULL) {
          if(strcmp(rules_tostring(obj, -1), array->val.s) != 0) {
            rules_unref(array->val.s);
          } else {
            doref = 0;
          }
        }
        array->val.s = rules_tostring(obj, -1);
        array->type = VCHAR;
        if(doref == 1) {
          rules_ref(array->val.s);
        }
      } break;
      case VNULL: {
        if(array->type == VCHAR && array->val.s != NULL) {
          rules_unref(array->val.s);
        }
        array->val.n = NULL;
        array->type = VNULL;
      } break;
    }
  }
  return 0;
}

static void rules_free_stack(void) {
  int x = 0;
  for(x=0;x<nrrules;x++) {
    struct varstack_t *node = (struct varstack_t *)rules[x]->userdata;
    if(node != NULL) {
      FREE(node->array);
      FREE(node);
    }
    rules[x]->userdata = NULL;
  }
}

void rules_parse_console(void *tmp) {
  int loop = 1;
  struct rule_stack_print_t *dat = (struct rule_stack_print_t *)tmp;
  while(loop) {
    switch(dat->step) {
      case 0: {
        char out[255] = { '\0' };
        snprintf_P((char *)&out, 255, PSTR("%s %s %s\n%s%d %s %d %s"), PSTR("===="), dat->name, PSTR("===="), PSTR("rule #"), dat->nr, PSTR("was executed in"), dat->time, PSTR("microseconds"));

        if(dat->client == WEBSERVER_MAX_CLIENTS) {
          if(heishamonSettings.logSerial1) {
  #if defined(ESP8266)
            Serial.print(millis());
            Serial.print(": ");
            Serial.println(out);
  #elif defined(ESP32)
            Serial.print(millis());
            Serial.print(": ");
            Serial.println(out);
  #endif
          }
          dat->client = 0;
          dat->step++;
          return rules_parse_console(tmp);
        } else {
          if(clients[dat->client].data.is_websocket == 1) {
            return websocket_write(&clients[dat->client++].data, out, strlen(out), dat);
          } else {
            dat->client++;
          }
        }
      } break;
      case 1: {
        if(dat->client == WEBSERVER_MAX_CLIENTS) {
          if(heishamonSettings.logSerial1) {
  #if defined(ESP8266)
            Serial.print(millis());
            Serial.print(": ");
            Serial.println(PSTR("\n>>> local variables\n"));
  #elif defined(ESP32)
            Serial.print(millis());
            Serial.print(": ");
            Serial.println(PSTR("\n>>> local variables\n"));
  #endif
          }
          dat->client = 0;
          dat->step++;
        } else {
          if(clients[dat->client].data.is_websocket == 1) {
            loop = 0;
            websocket_write_P(&clients[dat->client++].data, PSTR("\n>>> local variables\n"), strlen_P(PSTR("\n>>> local variables\n")), dat);
          } else {
            dat->client++;
          }
        }
      } break;
      case 3: {
        if(dat->client == WEBSERVER_MAX_CLIENTS) {
          if(heishamonSettings.logSerial1) {
  #if defined(ESP8266)
            Serial.print(millis());
            Serial.print(": ");
            Serial.println(PSTR("\n>>> global variables\n"));
  #elif defined(ESP32)
            Serial.print(millis());
            Serial.print(": ");
            Serial.println(PSTR("\n>>> global variables\n"));
  #endif
          }
          dat->client = 0;
          dat->step++;
        } else {
          if(clients[dat->client].data.is_websocket == 1) {
            loop = 0;
            websocket_write_P(&clients[dat->client++].data, PSTR("\n>>> global variables\n"), strlen_P(PSTR("\n>>> global variables\n")), dat);
          } else {
            dat->client++;
          }
        }
      } break;
      case 4:
      case 2: {
        char *out = NULL;
        struct array_t *array = NULL;
        uint16_t l = 0;

        if(dat->table != NULL) {
          if(dat->idx < dat->table->nr) {
            array = &dat->table->array[dat->idx];
            switch(array->type) {
              case VINTEGER: {
                l = snprintf_P(NULL, 0, PSTR("%2d %s = %d\n"), dat->idx, array->key, array->val.i);
              } break;
              case VFLOAT: {
                l = snprintf_P(NULL, 0, PSTR("%2d %s = %g\n"), dat->idx, array->key, array->val.f);
              } break;
              case VCHAR: {
                l = snprintf_P(NULL, 0, PSTR("%2d %s = %s\n"), dat->idx, array->key, array->val.s);
              } break;
              case VNULL: {
                l = snprintf_P(NULL, 0, PSTR("%d %s = NULL\n"), dat->idx, array->key);
              } break;
            }

            if((out = (char *)malloc(l+1)) == NULL) {
              logprintf_P(F("Not enough memory for rules console output %s:#%d"), __FUNCTION__, __LINE__);
              return;
            }
            memset(out, 0, l+1);

            switch(array->type) {
              case VINTEGER: {
                snprintf_P(out, l, PSTR("%2d %s = %d\n"), dat->idx, array->key, array->val.i);
              } break;
              case VFLOAT: {
                snprintf_P(out, l, PSTR("%2d %s = %g\n"), dat->idx, array->key, array->val.f);
              } break;
              case VCHAR: {
                snprintf_P(out, l, PSTR("%2d %s = %s\n"), dat->idx, array->key, array->val.s);
              } break;
              case VNULL: {
                snprintf_P(out, l, PSTR("%d %s = NULL\n"), dat->idx, array->key);
              } break;
            }

            if(dat->client == WEBSERVER_MAX_CLIENTS) {
              if(heishamonSettings.logSerial1) {
        #if defined(ESP8266)
                Serial.print(millis());
                Serial.print(": ");
                Serial.println(out);
        #elif defined(ESP32)
                Serial.print(millis());
                Serial.print(": ");
                Serial.println(out);
        #endif
              }
              if(dat->idx == dat->table->nr) {
                if(dat->step == 2) {
                  dat->table = &global_varstack;
                  dat->client = 0;
                  dat->idx = 0;
                  dat->step++;
                } else {
                  loop = 0;
                  free(tmp);
                  rules_free_stack();
                }
              } else {
                dat->client = 0;
                dat->idx++;
              }
            } else {
              if(clients[dat->client].data.is_websocket == 1) {
                loop = 0;
                websocket_write(&clients[dat->client++].data, out, strlen(out), tmp);
              } else {
                dat->client++;
              }
            }
            free(out);
          } else {
            dat->step++;
          }
        } else {
          if(dat->step == 2) {
            dat->table = &global_varstack;
            dat->client = 0;
            dat->idx = 0;
            dat->step++;
          } else {
            free(tmp);
            rules_free_stack();
            loop = 0;
          }
        }
      } break;
      case 5: {
        loop = 0;
      } break;
    }
  }
}

static void rules_print_stack(char *name, uint8_t nr, int time, struct varstack_t *table) {
  struct rule_stack_print_t *tmp = (struct rule_stack_print_t *)malloc(sizeof(struct rule_stack_print_t));
  if(tmp == NULL) {
#if defined(ESP8266) || defined(ESP32)
    Serial1.printf("Out of memory %s:#%d\n", __FUNCTION__, __LINE__);
    ESP.restart();
    exit(-1);
#elif defined(ESP32)
    Serial.printf("Out of memory %s:#%d\n", __FUNCTION__, __LINE__);
    ESP.restart();
    exit(-1);
#endif
  }
  tmp->route = 1;
  tmp->client = 0;
  tmp->step = 0;
  tmp->table = table;
  tmp->idx = 0;
  if((tmp->name = strdup(name)) == NULL) {
#if defined(ESP8266) || defined(ESP32)
    Serial1.printf("Out of memory %s:#%d\n", __FUNCTION__, __LINE__);
    ESP.restart();
    exit(-1);
#elif defined(ESP32)
    Serial.printf("Out of memory %s:#%d\n", __FUNCTION__, __LINE__);
    ESP.restart();
    exit(-1);
#endif
  }
  tmp->time = time;
  tmp->nr = nr;

  rules_parse_console(tmp);
}

void rules_timer_cb(int nr) {
  char *name = NULL;
  int x = 0, i = 0;

  i = snprintf_P(NULL, 0, PSTR("timer=%d"), nr);
  if((name = (char *)MALLOC(i+2)) == NULL) {
    OUT_OF_MEMORY
  }
  memset(name, 0, i+2);
  snprintf_P(name, i+1, PSTR("timer=%d"), nr);

  nr = rule_by_name(rules, nrrules, name);
  if(nr > -1) {
    timestamp.first = micros();

    int ret = rule_run(rules[nr], 0);

    timestamp.second = micros();

    if(ret == 0) {
      rules_print_stack(name, nr, timestamp.second - timestamp.first, (struct varstack_t *)rules[nr]->userdata);
    }
  }
  FREE(name);
}

void rules_setup(void) {
  if(!LittleFS.begin()) {
    return;
  }
    if (rule_options.event_cb == NULL) { //check if not initialized before
#ifdef ESP32
      if (mempool == NULL) { //make sure we only malloc if not done before
        mempool = (unsigned char *)ps_malloc(MEMPOOL_SIZE);  //in arduino IDE normal malloc causes big block to go to PSRAM if PSRAM is enabled. But seems to be unstable so for now don't enable PSRAM
        if (mempool == NULL) {
          logprintln_P(F("Mempool OOM"));
          OUT_OF_MEMORY
        }
      }
#endif  
    memset(mempool, 0, MEMPOOL_SIZE);

    logprintf_P(F("rules mempool size: %d"), MEMPOOL_SIZE);

    logprintln_P(F("reading rules"));

    memset(&rule_options, 0, sizeof(struct rule_options_t));
    rule_options.is_variable_cb = is_variable;
    rule_options.is_event_cb = is_event;
    rule_options.done_cb = rule_done_cb;
    rule_options.vm_value_set = vm_value_set;
    rule_options.vm_value_get = vm_value_get;
    rule_options.event_cb = event_cb;

  }
}

bool existsRulesFile(char *file) {
  if (LittleFS.begin() && (LittleFS.exists(file))) {
    File f = LittleFS.open(file, "r");
    if ((f) && (f.size() > 0)) {
      f.close();
      return true; //only return true if file exists and isn't empty
    }
  }
  return false;
}

int rules_parse(char *file) {
  if (existsRulesFile(file)) { //only parse an existing and not empty, file
    rules_setup(); //check there if done already
	
    File frules = LittleFS.open(file, "r");
    parsing = 1;

    if(nrrules > 0) {
      rules_free_stack();
      rules_gc(&rules, &nrrules);

      struct varstack_t *table = (struct varstack_t *)&global_varstack;
      if(table->array != NULL) {
        FREE(table->array);
      }
      table->nr = 0;
    }
    memset(mempool, 0, MEMPOOL_SIZE);

#define BUFFER_SIZE 128
    char content[BUFFER_SIZE];
    memset(content, 0, BUFFER_SIZE);
    int len = frules.size();
    int chunk = 0, len1 = 0;

    unsigned int txtoffset = alignedbuffer(MEMPOOL_SIZE-len-5);
	
    while(chunk*BUFFER_SIZE < len) {
      memset(content, 0, BUFFER_SIZE);
      frules.seek(chunk*BUFFER_SIZE, SeekSet);
      len1 = frules.readBytes(content, BUFFER_SIZE);
      memcpy(&mempool[txtoffset+(chunk*BUFFER_SIZE)], &content, alignedbuffer(len1));
      chunk++;
    }
    frules.close();

    struct pbuf mem;
    struct pbuf input;
    memset(&mem, 0, sizeof(struct pbuf));
    memset(&input, 0, sizeof(struct pbuf));

    mem.payload = mempool;
    mem.len = 0;
    mem.tot_len = MEMPOOL_SIZE;

    input.payload = &mempool[txtoffset];
    input.len = txtoffset;
    input.tot_len = len;

    int ret = 0;
    while((ret = rule_initialize(&input, &rules, &nrrules, &mem, NULL)) == 0) {
      input.payload = &mempool[input.len];
    }

    logprintf_P(F("rules memory used: %d / %d"), mem.len, mem.tot_len);

    /*
     * Clear all timers
     */
    struct timerqueue_t *node = NULL;
    while((node = timerqueue_pop()) != NULL) {
      FREE(node);
    }

    struct varstack_t *table = (struct varstack_t *)&global_varstack;
    if(table->array != NULL) {
      FREE(table->array);
    }
    table->nr = 0;

    if(ret == -1) {
      if(nrrules > 0) {
        rules_free_stack();
        rules_gc(&rules, &nrrules);
      }
      return -1;
    }

    parsing = 0;
    return 0;
  } else {
	  return -2; //empty file or not existing
  }
}

void rules_event_cb(const char *prefix, const char *name) {
  uint8_t len = strlen(name), len1 = strlen(prefix), tlen = 0;
  char buf[100] = { '\0' };
  snprintf_P((char *)&buf, 100, PSTR("%s%s"), prefix, name);
  int8_t nr = rule_by_name(rules, nrrules, (char *)buf);
  if(nr > -1) {
    timestamp.first = micros();

    int ret = rule_run(rules[nr], 0);

    timestamp.second = micros();

    if(ret == 0) {
      rules_print_stack((char *)name, rules[nr]->nr, timestamp.second - timestamp.first, (struct varstack_t *)rules[nr]->userdata);
    }

    return;
  }
}

void rules_boot(void) {
  int8_t nr = rule_by_name(rules, nrrules, (char *)"System#Boot");
  if(nr > -1) {
    timestamp.first = micros();

    int ret = rule_run(rules[nr], 0);

    timestamp.second = micros();

    if(ret == 0) {
      rules_print_stack((char *)"System#Boot", nr, timestamp.second - timestamp.first, (struct varstack_t *)rules[nr]->userdata);
    }
  }
}

void rules_deinitialize() {
  if (rule_options.event_cb != NULL) { 
    logprintln_P(F("Deinitialize rules engine..."));
#ifdef ESP32
    FREE(mempool);
#endif 
    if(nrrules > 0) {
      for(int i=0;i<nrrules;i++) {
        if(rules[i]->userdata != NULL) {
          FREE(rules[i]->userdata);
        }
      }
      rules_gc(&rules, &nrrules);
      rules_free_stack();
    }

    // set this to NULL so a new initialize can start if necessary. 
    rule_options.event_cb = NULL;
  }
}

