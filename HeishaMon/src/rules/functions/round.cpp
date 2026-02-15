/*
  Copyright (C) CurlyMo

  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifdef ESP8266
  #pragma GCC diagnostic warning "-fpermissive"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../../common/uint32float.h"
#include "../../common/log.h"
#include "../function.h"
#include "../rules.h"

int8_t rule_function_round_callback(void) {
  float x = 0, z = 0;
  uint8_t dec = 0;
  uint8_t nr = rules_gettop(), y = nr;

  if(nr > 2) {
    return -1;
  } else if(nr == 2) {
    switch(rules_type(nr)) {
      case VINTEGER: {
        dec = rules_tointeger(nr);
      } break;
      default: {
        logprintf_P(F("ERROR: round 2nd argument can only be an integer"));
        while(nr > 0) {
          rules_remove(nr--);
        }
        rules_pushnil();
        return -1;
      } break;
    }
    rules_remove(nr--);
  }

  switch(rules_type(nr)) {
    case VNULL: {
      rules_remove(nr--);
      rules_pushnil();
      return 0;
    } break;
    case VINTEGER: {
      x = (float)rules_tointeger(nr);
    } break;
    case VFLOAT: {
      x = rules_tofloat(nr);
    } break;
    default: {
      logprintf_P(F("ERROR: round 1st argument can only be a number"));
      while(nr > 0) {
        rules_remove(nr--);
      }
      rules_pushnil();
      return -1;
    } break;
  }
  rules_remove(nr--);

  if(modff(x, &z) == 0) {
#ifdef DEBUG
    printf("\tround = %d\n", round(x));
#endif
    rules_pushinteger(round(x));
  } else {
    if(y == 2) {
      uint8_t size = snprintf(NULL, 0, "%.*f", dec, x)+1;
      char buf[size] = { '\0' };
      snprintf((char *)&buf, size, "%.*f", dec, x);
#ifdef DEBUG
      printf("\tround = %f\n", atof(buf));
#endif
      rules_pushfloat(atof(buf));
    } else {
#ifdef DEBUG
      printf("\tround = %d\n", round(x));
#endif
      rules_pushinteger(round(x));
    }
  }

  return 0;
}
