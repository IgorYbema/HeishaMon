/* Minimal host shim so src/common/log.h compiles off-device. */
#ifndef _ARDUINO_SHIM_H_
#define _ARDUINO_SHIM_H_

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

typedef char __FlashStringHelper;

#ifndef PSTR
#define PSTR(x) (x)
#endif
#define snprintf_P snprintf
#define sprintf_P sprintf
#define strncmp_P strncmp
#define strlen_P strlen
#define memcpy_P memcpy

void digitalWrite(int pin, int state);
int digitalRead(int pin);

#endif
