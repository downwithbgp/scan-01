/* Scan 01 — key layer implementation (task T5, vision §4.2/§4.3)
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
 * The interaction model (vision §3, seven rules):
 *   - knob = volume (not a key — T6)
 *   - PTT = HOLD / long = CAPTURE
 *   - digits = a car number, `*` = the decimal point
 *   - short press = digit or nav; long press = the printed label
 *   - EXIT = back to SCAN; long-EXIT = HOME; no dead ends
 *
 * Host-tested in tests/test_keys.c — the behavior matrix (state × key ×
 * press-type) lives there. No hardware dependencies in this file.
 */

#include <string.h>

#include "scan01_keys.h"

/* ---- timing (10 ms ticks; P0-track-validation items, not UI settings) ---- */
#define PTT_HOLD_10MS        80      /* 0.8 s — long-PTT = CAPTURE (tasks.md) */
#define TYPE_TIMEOUT_10MS    150     /* 1.5 s — commit after the last key */

#define TYPE_BUF_MAX         9       /* 3 int + point + 5 frac ("450.8875") */

static Scan01UiState_t g_ui_state;
static bool            g_ptt_armed;        /* PTT down and hold timer running */
static uint16_t        g_ptt_hold_10ms;
static bool            g_ptt_capture_latch; /* hold expired → CAPTURE on release */
static Scan01UiState_t g_ptt_press_state; /* state at PTT press; a release in a
                                             different state is consumed (the
                                             press already acted — e.g. CAPTURE
                                             SAVE switched us to SCAN) */

static char            g_type_buf[TYPE_BUF_MAX + 1];
static uint8_t         g_type_len;
static bool            g_type_freq;        /* decimal point present */
static bool            g_type_autocommit;  /* CAPTURE disables it (vision §4.5) */
static uint16_t        g_type_timeout_10ms;
static char            g_type_entry[TYPE_BUF_MAX + 1]; /* last committed entry */
static bool            g_type_commit_pending;

static bool TypingIsActive(void)
{
    return g_type_len > 0;
}

static bool StateIsListening(Scan01UiState_t state)
{
    return state == SCAN01_UI_SCAN || state == SCAN01_UI_HOLD ||
           state == SCAN01_UI_LIST || state == SCAN01_UI_BRD ||
           state == SCAN01_UI_WX;
}

/* Does the buffer hold exactly one plain digit — i.e. the user is still
 * holding the very first key of an entry? That is the only situation where a
 * held digit is a label long-press (0→BRD, 5→WX, 9→CALL): a clean hold from
 * idle. Anything more complex is an entry, and labels are a promise, not a
 * trap (tasks.md T5). */
static bool BufferIsSingleDigit(char digit)
{
    return g_type_len == 1 && g_type_buf[0] == digit;
}

static void TypeCancel(void)
{
    g_type_len = 0;
    g_type_buf[0] = 0;
    g_type_freq = false;
    g_type_timeout_10ms = 0;
    g_type_commit_pending = false;
}

static void TypeAppend(char c)
{
    if (g_type_len >= TYPE_BUF_MAX)
        return;                             /* buffer full — ignore */
    if (!g_type_freq) {
        /* number mode: 1–3 digits, then an optional suffix letter */
        if (c >= '0' && c <= '9') {
            if (g_type_len >= 3)
                return;
            if (g_type_len > 0 &&
                g_type_buf[g_type_len - 1] >= 'A' && g_type_buf[g_type_len - 1] <= 'Z')
                return;                     /* digits never follow the suffix */
        } else {
            return;                         /* suffix handled by ▲/▼ only */
        }
    } else {
        /* frequency mode: digits go to the frac part once the point exists */
        bool point_seen = false;
        uint8_t frac_len = 0;
        for (uint8_t i = 0; i < g_type_len; i++) {
            if (g_type_buf[i] == '.')
                point_seen = true;
            else if (point_seen)
                frac_len++;
        }
        if (c == '.') {
            return;                         /* point already set */
        }
        if (point_seen && frac_len >= 5)
            return;                         /* frac part full */
    }
    g_type_buf[g_type_len++] = c;
    g_type_buf[g_type_len] = 0;
    g_type_timeout_10ms = TYPE_TIMEOUT_10MS;
}

static void TypeDelete(void)
{
    if (g_type_len == 0)
        return;
    g_type_len--;
    if (g_type_buf[g_type_len] == '.')
        g_type_freq = false;
    g_type_buf[g_type_len] = 0;
    g_type_timeout_10ms = TYPE_TIMEOUT_10MS;
}

static void TypeTogglePoint(void)
{
    if (g_type_freq || g_type_len == 0 || g_type_len > 3)
        return;                             /* number mode only, 1–3 digits */
    if (g_type_buf[g_type_len - 1] >= 'A' && g_type_buf[g_type_len - 1] <= 'Z')
        return;                             /* suffix letter and point don't mix */
    g_type_buf[g_type_len++] = '.';
    g_type_buf[g_type_len] = 0;
    g_type_freq = true;
    g_type_timeout_10ms = TYPE_TIMEOUT_10MS;
}

static bool TypeCycleSuffix(bool up)
{
    if (g_type_freq || g_type_len == 0)
        return false;                       /* suffix only in number mode */
    if (g_type_buf[g_type_len - 1] >= 'A' && g_type_buf[g_type_len - 1] <= 'Z') {
        /* a suffix letter is present — cycle it */
        if (up) {
            g_type_buf[g_type_len - 1] =
                (g_type_buf[g_type_len - 1] == 'Z') ? 'A' : (char)(g_type_buf[g_type_len - 1] + 1);
        } else {
            g_type_buf[g_type_len - 1] =
                (g_type_buf[g_type_len - 1] == 'A') ? 'Z' : (char)(g_type_buf[g_type_len - 1] - 1);
        }
    } else if (g_type_len <= 2) {
        g_type_buf[g_type_len++] = up ? 'A' : 'Z';
        g_type_buf[g_type_len] = 0;
    } else {
        return false;                       /* 3 digits, no room for a suffix */
    }
    g_type_timeout_10ms = TYPE_TIMEOUT_10MS;
    return true;
}

void SCAN01_KEYS_Init(void)
{
    g_ui_state = SCAN01_UI_SCAN;
    g_ptt_armed = false;
    g_ptt_hold_10ms = 0;
    g_ptt_capture_latch = false;
    g_ptt_press_state = SCAN01_UI_SCAN;
    g_type_len = 0;
    g_type_buf[0] = 0;
    g_type_freq = false;
    g_type_autocommit = true;
    g_type_timeout_10ms = 0;
    g_type_entry[0] = 0;
    g_type_commit_pending = false;
}

void SCAN01_KEYS_SetUiState(Scan01UiState_t state)
{
    bool leaving_capture = (g_ui_state == SCAN01_UI_CAPTURE && state != SCAN01_UI_CAPTURE);
    g_ui_state = state;
    if (leaving_capture)
        SCAN01_TYPE_SetAutoCommit(true);    /* CAPTURE is the only off-switch (§4.5) */
    /* entering a new state aborts a pending entry and hold timer */
    if (!StateIsListening(state)) {
        TypeCancel();
        g_ptt_armed = false;
        g_ptt_hold_10ms = 0;
        g_ptt_capture_latch = false;
    }
}

Scan01UiState_t SCAN01_KEYS_GetUiState(void)
{
    return g_ui_state;
}

bool SCAN01_KEYS_IsTyping(void)
{
    return TypingIsActive();
}

/* ---- typing buffer API ---- */

void SCAN01_TYPE_Reset(void)
{
    TypeCancel();
}

void SCAN01_TYPE_SetAutoCommit(bool enabled)
{
    g_type_autocommit = enabled;
    if (!enabled)
        g_type_timeout_10ms = 0;
}

bool SCAN01_TYPE_IsFreq(void)
{
    return g_type_freq;
}

const char *SCAN01_TYPE_GetBuffer(void)
{
    return g_type_buf;
}

/* Drain a committed entry (timeout). Returns false when nothing is pending.
 * T6 polls this every frame; the entry stays valid until the next commit. */
bool SCAN01_KEYS_PollCommit(char *out, uint8_t size)
{
    if (!g_type_commit_pending)
        return false;
    g_type_commit_pending = false;
    if (out != NULL && size > 0) {
        uint8_t n = (uint8_t)strlen(g_type_entry);
        if (n > size - 1)
            n = size - 1;
        memcpy(out, g_type_entry, n);
        out[n] = 0;
    }
    return true;
}

/* ---- 10 ms tick: PTT hold timer + typing timeout ---- */

void SCAN01_KEYS_Tick10ms(void)
{
    if (g_ptt_armed) {
        if (++g_ptt_hold_10ms >= PTT_HOLD_10MS) {
            g_ptt_armed = false;
            g_ptt_hold_10ms = 0;
            /* long-PTT while typing cancels the entry (PTT wins) */
            TypeCancel();
            /* handled by the caller through the return of ProcessKey? No —
             * the tick cannot return an action. The CAPTURE latch is set
             * here; ProcessKey only consumes it on the PTT release. */
            g_ptt_capture_latch = true;
        }
    }
    if (g_type_autocommit && g_type_timeout_10ms > 0) {
        if (--g_type_timeout_10ms == 0) {
            bool has_entry = g_type_len > 0;
            if (has_entry)
                strcpy(g_type_entry, g_type_buf);
            TypeCancel();
            g_type_commit_pending = has_entry;
        }
    }
}

/* ---- key events ---- */

static Scan01Action_t HandleListeningKey(KEY_Code_t key, bool held);

Scan01Action_t SCAN01_KEYS_ProcessKey(KEY_Code_t key, bool bKeyPressed, bool bKeyHeld)
{
    if (key == KEY_INVALID)
        return SCAN01_ACT_NONE;

    /* ---- PTT (press/release only from the base; our own hold timer) ---- */
    if (key == KEY_PTT) {
        if (bKeyPressed) {
            g_ptt_press_state = g_ui_state; /* any release lands elsewhere: consumed */
            if (g_ui_state == SCAN01_UI_SETUP)
                return SCAN01_ACT_BACK;
            if (g_ui_state == SCAN01_UI_CAPTURE)
                return SCAN01_ACT_SAVE;     /* save on press; release is consumed */
            /* listening state: PTT wins over a pending entry; arm the hold */
            TypeCancel();
            g_ptt_armed = true;
            g_ptt_hold_10ms = 0;
            g_ptt_capture_latch = false;
            return SCAN01_ACT_NONE;
        }
        /* release: CAPTURE (after a full hold) or HOLD/RESUME (quick tap) */
        if (g_ui_state == SCAN01_UI_CAPTURE || g_ui_state == SCAN01_UI_SETUP)
            return SCAN01_ACT_NONE;         /* the press already handled it */
        if (g_ui_state != g_ptt_press_state)
            return SCAN01_ACT_NONE;         /* state changed mid-press: consumed */
        if (g_ptt_capture_latch) {
            g_ptt_capture_latch = false;
            g_ptt_armed = false;
            g_ptt_hold_10ms = 0;
            return SCAN01_ACT_CAPTURE;
        }
        g_ptt_armed = false;
        g_ptt_hold_10ms = 0;
        return (g_ui_state == SCAN01_UI_SCAN) ? SCAN01_ACT_HOLD : SCAN01_ACT_RESUME;
    }

    return HandleListeningKey(key, bKeyHeld);
}

static Scan01Action_t HandleListeningKey(KEY_Code_t key, bool held)
{
    const bool typing = TypingIsActive();

    /* ---- held events ---- */
    if (held) {
        if (g_ui_state == SCAN01_UI_SETUP) {
            /* held ▲▼ edits the focused value (vision §5.5: UP/DOWN + knob edit) */
            if (key == KEY_UP)
                return SCAN01_ACT_VALUE_UP;
            if (key == KEY_DOWN)
                return SCAN01_ACT_VALUE_DOWN;
            return (key == KEY_EXIT) ? SCAN01_ACT_HOME : SCAN01_ACT_NONE;
        }
        if (typing) {
            switch (key) {
            case KEY_0:
                return BufferIsSingleDigit('0') ? SCAN01_ACT_BRD : SCAN01_ACT_NONE;
            case KEY_5:
                return BufferIsSingleDigit('5') ? SCAN01_ACT_WX : SCAN01_ACT_NONE;
            case KEY_9:
                return BufferIsSingleDigit('9') ? SCAN01_ACT_MYDRIVER : SCAN01_ACT_NONE;
            case KEY_UP:
                return TypeCycleSuffix(true) ? SCAN01_ACT_TYPE_UPDATE : SCAN01_ACT_NONE;
            case KEY_DOWN:
                return TypeCycleSuffix(false) ? SCAN01_ACT_TYPE_UPDATE : SCAN01_ACT_NONE;
            case KEY_EXIT:
                TypeCancel();               /* long-EXIT clears the entry first */
                return SCAN01_ACT_NONE;
            default:
                return SCAN01_ACT_NONE;     /* labels are a promise, not a trap */
            }
        }
        switch (key) {
        case KEY_0:     return SCAN01_ACT_BRD;
        case KEY_5:     return SCAN01_ACT_WX;
        case KEY_9:     return SCAN01_ACT_MYDRIVER;
        case KEY_STAR:  return (g_ui_state == SCAN01_UI_HOLD || g_ui_state == SCAN01_UI_LIST)
                              ? SCAN01_ACT_LOCKOUT : SCAN01_ACT_NONE;
        case KEY_EXIT:  return SCAN01_ACT_HOME;
        case KEY_MENU:  return SCAN01_ACT_KEYLOCK;
        case KEY_SIDE1:    return SCAN01_ACT_MUTE_TOGGLE;
        case KEY_UP:
            return (g_ui_state == SCAN01_UI_LIST) ? SCAN01_ACT_NAV_UP
                                                  : SCAN01_ACT_VOL_UP; /* held = volume */
        case KEY_DOWN:
            return (g_ui_state == SCAN01_UI_LIST) ? SCAN01_ACT_NAV_DOWN
                                                  : SCAN01_ACT_VOL_DOWN;
        default:        return SCAN01_ACT_NONE;
        }
    }

    /* ---- short presses ---- */
    if (typing) {
        switch (key) {
        case KEY_0: case KEY_1: case KEY_2: case KEY_3: case KEY_4:
        case KEY_5: case KEY_6: case KEY_7: case KEY_8: case KEY_9:
            TypeAppend((char)('0' + (key - KEY_0)));
            return SCAN01_ACT_TYPE_UPDATE;
        case KEY_STAR:
            TypeTogglePoint();
            return SCAN01_ACT_TYPE_UPDATE;
        case KEY_EXIT:
            TypeDelete();
            return SCAN01_ACT_TYPE_UPDATE;
        case KEY_UP:
            return TypeCycleSuffix(true) ? SCAN01_ACT_TYPE_UPDATE : SCAN01_ACT_NONE;
        case KEY_DOWN:
            return TypeCycleSuffix(false) ? SCAN01_ACT_TYPE_UPDATE : SCAN01_ACT_NONE;
        case KEY_SIDE1:    return SCAN01_ACT_MUTE;       /* harmless mid-entry */
        case KEY_MENU:  TypeCancel(); return SCAN01_ACT_SETUP;
        case KEY_SIDE2:    TypeCancel(); return SCAN01_ACT_GROUP;
        case KEY_F:     TypeCancel(); return SCAN01_ACT_FAVORITES;
        default:        return SCAN01_ACT_NONE;
        }
    }

    if (g_ui_state == SCAN01_UI_SETUP) {
        /* SETUP is a form: M = next page (§5.5), EXIT/PTT leave, * escapes */
        switch (key) {
        case KEY_MENU:
            return SCAN01_ACT_SETUP_NEXT;
        case KEY_EXIT:
        case KEY_PTT:
            return SCAN01_ACT_BACK;
        case KEY_STAR:
            return SCAN01_ACT_SCAN;
        case KEY_UP:
            return SCAN01_ACT_NAV_UP;
        case KEY_DOWN:
            return SCAN01_ACT_NAV_DOWN;
        default:
            return SCAN01_ACT_NONE;
        }
    }

    switch (key) {
    case KEY_0: case KEY_1: case KEY_2: case KEY_3: case KEY_4:
    case KEY_5: case KEY_6: case KEY_7: case KEY_8: case KEY_9:
        if (g_ui_state == SCAN01_UI_WX)
            return SCAN01_ACT_NONE;         /* WX never configures */
        TypeAppend((char)('0' + (key - KEY_0)));
        return SCAN01_ACT_TYPE_UPDATE;      /* typing starts */
    case KEY_STAR:
        return SCAN01_ACT_SCAN;             /* printed label SCAN */
    case KEY_UP:
        return (g_ui_state == SCAN01_UI_SCAN) ? SCAN01_ACT_LIST : SCAN01_ACT_NAV_UP;
    case KEY_DOWN:
        return (g_ui_state == SCAN01_UI_SCAN) ? SCAN01_ACT_LIST : SCAN01_ACT_NAV_DOWN;
    case KEY_F:
        return SCAN01_ACT_FAVORITES;
    case KEY_SIDE1:
        return SCAN01_ACT_MUTE;
    case KEY_SIDE2:
        return SCAN01_ACT_GROUP;
    case KEY_MENU:
        return (g_ui_state == SCAN01_UI_SETUP) ? SCAN01_ACT_BACK : SCAN01_ACT_SETUP;
    case KEY_EXIT:
        return (g_ui_state == SCAN01_UI_SETUP) ? SCAN01_ACT_BACK :
               (g_ui_state == SCAN01_UI_SCAN)  ? SCAN01_ACT_NONE : SCAN01_ACT_SCAN;
    default:
        return SCAN01_ACT_NONE;             /* unbound keys (SETUP-cased above) */
    }
}
