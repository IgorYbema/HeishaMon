/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifndef __RULES_H_
#define __RULES_H_

#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include <Arduino.h>

#include "src/common/mem.h"

extern uint8_t nrrules;

typedef struct rule_stack_print_t {
  uint8_t route;
  uint8_t client;
  uint8_t step;
  struct varstack_t *table;
  char *name;
  int time;
  uint8_t nr;
  uint8_t idx;
} rule_stack_print_t;

void rules_boot(void);
void rules_deinitialize(void);
int rules_parse(char *file);
void rules_setup(void);
void rules_timer_cb(int nr);
void rules_event_cb(const char *prefix, const char *name);
void rules_execute(void);
void rules_parse_console(void *tmp);

#endif
