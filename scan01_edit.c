/* Scan 01 — multi-tap name editor implementation (task T6b, vision §4.5)
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
 * Phone-keypad multi-tap: 2=ABC 3=DEF 4=GHI 5=JKL 6=MNO 7=PQRS 8=TUV
 * 9=WXYZ, 0 = space. Same key inside the 1.5 s window cycles to the next
 * letter (wrapping); a different key or the window expiry commits the
 * pending letter and starts a new one.
 */

#include <string.h>

#include "scan01_edit.h"

static const char *const KEY_LETTERS[10] = {
    " ",                 /* 0 — space */
    "",                  /* 1 — no letters */
    "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ"
};

#define EDIT_WINDOW_10MS 150     /* 1.5 s — same as the typing timeout */

static char    g_buf[SCAN01_EDIT_MAX + 1];
static uint8_t g_len;
static uint8_t g_pending_key;    /* KEY_2..KEY_9; 0 = none (committed) */
static uint8_t g_pending_idx;    /* letter index within the key's set */
static uint16_t g_window_10ms;

void SCAN01_EDIT_Reset(void)
{
    g_len = 0;
    g_buf[0] = 0;
    g_pending_key = 0;
    g_pending_idx = 0;
    g_window_10ms = 0;
}

const char *SCAN01_EDIT_GetBuffer(void)
{
    return g_buf;
}

void SCAN01_EDIT_Tick10ms(void)
{
    if (g_window_10ms > 0 && --g_window_10ms == 0)
        g_pending_key = 0;                  /* the pending letter is committed */
}

void SCAN01_EDIT_Delete(void)
{
    g_pending_key = 0;
    g_window_10ms = 0;
    if (g_len > 0) {
        g_len--;
        g_buf[g_len] = 0;
    }
}

void SCAN01_EDIT_Clear(void)
{
    g_len = 0;
    g_buf[0] = 0;
    g_pending_key = 0;
    g_pending_idx = 0;
    g_window_10ms = 0;
}

bool SCAN01_EDIT_ProcessKey(KEY_Code_t key)
{
    if (key > KEY_9)
        return false;                       /* not a letter key */

    if (key == KEY_0 || key == KEY_1) {
        g_pending_key = 0;                  /* commit the pending letter */
        if (key == KEY_0) {
            if (g_len < SCAN01_EDIT_MAX) {
                g_buf[g_len++] = ' ';
                g_buf[g_len] = 0;
            }
        }
        return true;
    }

    const char *letters = KEY_LETTERS[key - KEY_0];

    if (g_pending_key == key) {
        /* same key inside the window: cycle the pending letter (wraps) */
        g_pending_idx = (uint8_t)((g_pending_idx + 1) % (uint8_t)strlen(letters));
        g_buf[g_len - 1] = letters[g_pending_idx];
    } else {
        /* new letter: append the first letter of the key */
        if (g_len >= SCAN01_EDIT_MAX) {
            g_pending_key = 0;              /* full — consume, commit pending */
            return true;
        }
        g_pending_key = key;
        g_pending_idx = 0;
        g_buf[g_len++] = letters[0];
        g_buf[g_len] = 0;
    }
    g_window_10ms = EDIT_WINDOW_10MS;
    return true;
}
