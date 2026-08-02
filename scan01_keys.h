/* Scan 01 — key layer (task T5, vision §4.2/§4.3)
 *
 * Copyright 2026 Vadim Petrov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Pure logic: no hardware calls, no globals beyond module state. Host-tested
 * in tests/test_keys.c; the firmware wires it in T6 (app.c ProcessKey +
 * APP_TimeSlice10ms). The base's 600 ms held events drive the label
 * long-presses; PTT has its own layer-owned hold timer (the base treats PTT
 * as press/release only).
 */

#ifndef SCAN01_KEYS_H
#define SCAN01_KEYS_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/keyboard.h"

/* The UI states T6's state machine will drive. The key layer only needs
 * enough to route presses; CAPTURE is the modal entry form. */
typedef enum {
    SCAN01_UI_SCAN = 0,
    SCAN01_UI_HOLD,
    SCAN01_UI_LIST,
    SCAN01_UI_SETUP,
    SCAN01_UI_BRD,
    SCAN01_UI_WX,
    SCAN01_UI_CAPTURE,
} Scan01UiState_t;

typedef enum {
    SCAN01_ACT_NONE = 0,

    /* PTT — the biggest button */
    SCAN01_ACT_HOLD,        /* PTT short in SCAN: lock the car you're hearing */
    SCAN01_ACT_RESUME,      /* PTT short in HOLD/LIST/BRD/WX: back to scanning */
    SCAN01_ACT_CAPTURE,     /* PTT long (~0.8 s): capture last-heard */
    SCAN01_ACT_SAVE,        /* PTT short in CAPTURE: save the entry */

    /* label long-presses */
    SCAN01_ACT_BRD,         /* 0 long (printed FM) */
    SCAN01_ACT_WX,          /* 5 long (printed NOAA) */
    SCAN01_ACT_MYDRIVER,    /* 9 long (printed CALL) */
    SCAN01_ACT_HOME,        /* EXIT long: LIST in RACE mode, from anywhere */

    /* navigation */
    SCAN01_ACT_SCAN,        /* * short (printed SCAN): resume scanning */
    SCAN01_ACT_LOCKOUT,     /* * long in HOLD/LIST: drop this car from the scan */
    SCAN01_ACT_FAVORITES,   /* # short (printed F) */
    SCAN01_ACT_MUTE,        /* F1 short: 10 s mute */
    SCAN01_ACT_MUTE_TOGGLE, /* F1 long: persistent mute */
    SCAN01_ACT_GROUP,       /* F2 short: cycle scan group */
    SCAN01_ACT_SETUP,       /* M short */
    SCAN01_ACT_KEYLOCK,     /* M long */
    SCAN01_ACT_BACK,        /* EXIT/PTT/M short in SETUP; EXIT in sub-states */
    SCAN01_ACT_LIST,        /* UP/DOWN in SCAN: open the LIST */
    SCAN01_ACT_NAV_UP,      /* UP (LIST scroll, HOLD prev car, SETUP value) */
    SCAN01_ACT_NAV_DOWN,    /* DOWN */

    /* typing (vision §4.3) */
    SCAN01_ACT_TYPE_UPDATE, /* buffer changed — redraw the big digits */
} Scan01Action_t;

void      SCAN01_KEYS_Init(void);
void      SCAN01_KEYS_SetUiState(Scan01UiState_t state);
Scan01UiState_t SCAN01_KEYS_GetUiState(void);

/* One key event from the base: (press at debounce) and (held at 600 ms).
 * Returns the Scan01 action; NONE = consume-and-ignore. */
Scan01Action_t SCAN01_KEYS_ProcessKey(KEY_Code_t key, bool bKeyPressed, bool bKeyHeld);

/* 10 ms tick: PTT hold timer + typing timeout. Call from APP_TimeSlice10ms. */
void SCAN01_KEYS_Tick10ms(void);

bool SCAN01_KEYS_IsTyping(void);

/* ---- typing buffer (vision §4.3) ---- */
void        SCAN01_TYPE_Reset(void);
void        SCAN01_TYPE_SetAutoCommit(bool enabled); /* CAPTURE disables it */
bool        SCAN01_TYPE_IsFreq(void);                /* has a decimal point */
const char *SCAN01_TYPE_GetBuffer(void);             /* "24A" or "451.1125" */

/* Drain a timeout-committed entry ("24A" / "451.1125"); false when none. */
bool SCAN01_KEYS_PollCommit(char *out, uint8_t size);

#endif /* SCAN01_KEYS_H */
