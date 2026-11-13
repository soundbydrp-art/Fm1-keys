/* FM-1 KEYS: a tine electric piano and a physical-modelling voice (strings, tubes, struck and bowed materials),
 * with a chord engine. The app's half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "FM-1 KEYS"
#define APP_SPLASH_RGB RGB(255, 178, 92)
#define APP_BLOCK 128u                   /* frames per audio block: 2.9 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity from the keys (lib/touch.h) */

enum { OBJ_SESSION, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
