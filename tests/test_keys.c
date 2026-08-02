/* Scan 01 — key layer behavior matrix (task T5, vision §4.2/§4.3)
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
 * The gate for T5: state × key × press-type → action, reviewed against the
 * vision §4.2 table. Pure host test — no hardware.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scan01_keys.h"

static int g_checks = 0;
static int g_failures = 0;

static void expect(bool cond, const char *what)
{
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL: %s\n", what);
    }
}

static void expect_action(Scan01Action_t got, Scan01Action_t want, const char *what)
{
    g_checks++;
    if (got != want) {
        g_failures++;
        printf("FAIL: %s — got %d want %d\n", what, (int)got, (int)want);
    }
}

/* helpers: the base fires press(true,false), release(false,false), and for
 * held keys a later (true,true); PTT arrives only as press/release. */
static Scan01Action_t press(KEY_Code_t k)   { return SCAN01_KEYS_ProcessKey(k, true,  false); }
static Scan01Action_t release(KEY_Code_t k) { return SCAN01_KEYS_ProcessKey(k, false, false); }
static Scan01Action_t held(KEY_Code_t k)    { return SCAN01_KEYS_ProcessKey(k, true,  true);  }

static void ticks(int n)
{
    for (int i = 0; i < n; i++)
        SCAN01_KEYS_Tick10ms();
}

static void type_key(char digit)
{
    expect_action(press(KEY_0 + (digit - '0')), SCAN01_ACT_TYPE_UPDATE, "digit starts typing");
}

static void reset(void)
{
    SCAN01_KEYS_Init();
    SCAN01_KEYS_SetUiState(SCAN01_UI_SCAN);
}

/* ---- the behavior matrix (vision §4.2) ---- */

static void test_scan_matrix(void)
{
    reset();

    /* PTT short = HOLD (action on release); long = CAPTURE */
    expect_action(press(KEY_PTT), SCAN01_ACT_NONE, "SCAN: PTT press arms hold");
    expect_action(release(KEY_PTT), SCAN01_ACT_HOLD, "SCAN: PTT short → HOLD");
    expect_action(press(KEY_PTT), SCAN01_ACT_NONE, "SCAN: PTT press (2)");
    ticks(79);
    expect_action(release(KEY_PTT), SCAN01_ACT_HOLD, "SCAN: PTT 0.79 s → HOLD (not CAPTURE)");
    expect_action(press(KEY_PTT), SCAN01_ACT_NONE, "SCAN: PTT press (3)");
    ticks(80);
    expect_action(release(KEY_PTT), SCAN01_ACT_CAPTURE, "SCAN: PTT 0.80 s → CAPTURE");

    /* digits: typing starts; the buffer holds them */
    expect_action(press(KEY_2), SCAN01_ACT_TYPE_UPDATE, "SCAN: digit → typing");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "2") == 0, "SCAN: buffer '2'");
    expect_action(press(KEY_4), SCAN01_ACT_TYPE_UPDATE, "SCAN: second digit");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "24") == 0, "SCAN: buffer '24'");
    SCAN01_TYPE_Reset();

    /* label long-presses from idle */
    expect_action(held(KEY_0), SCAN01_ACT_BRD, "SCAN: 0 held → BRD");
    expect_action(held(KEY_5), SCAN01_ACT_WX, "SCAN: 5 held → WX");
    expect_action(held(KEY_9), SCAN01_ACT_MYDRIVER, "SCAN: 9 held → my driver");
    expect_action(held(KEY_1), SCAN01_ACT_NONE, "SCAN: 1 held → nothing (no label)");
    expect_action(held(KEY_STAR), SCAN01_ACT_NONE, "SCAN: * held → nothing (no car to lock)");

    /* navigation */
    expect_action(press(KEY_UP), SCAN01_ACT_LIST, "SCAN: UP → LIST");
    expect_action(press(KEY_DOWN), SCAN01_ACT_LIST, "SCAN: DOWN → LIST");
    expect_action(press(KEY_STAR), SCAN01_ACT_SCAN, "SCAN: * short → SCAN (no-op)");
    expect_action(press(KEY_F), SCAN01_ACT_FAVORITES, "SCAN: # → favorites");
    expect_action(press(KEY_SIDE1), SCAN01_ACT_MUTE, "SCAN: F1 → mute");
    expect_action(held(KEY_SIDE1), SCAN01_ACT_MUTE_TOGGLE, "SCAN: F1 held → mute toggle");
    expect_action(press(KEY_SIDE2), SCAN01_ACT_GROUP, "SCAN: F2 → group cycle");
    expect_action(press(KEY_MENU), SCAN01_ACT_SETUP, "SCAN: M → SETUP");
    expect_action(held(KEY_MENU), SCAN01_ACT_KEYLOCK, "SCAN: M held → keylock");
    expect_action(press(KEY_EXIT), SCAN01_ACT_NONE, "SCAN: EXIT → no-op");
    expect_action(held(KEY_EXIT), SCAN01_ACT_HOME, "SCAN: EXIT held → HOME");
    expect_action(press(KEY_SIDE1), SCAN01_ACT_MUTE, "SCAN: SIDE1 (F1) → MUTE");
    expect_action(press(KEY_SIDE2), SCAN01_ACT_GROUP, "SCAN: SIDE2 (F2) → group");
}

static void test_hold_matrix(void)
{
    reset();
    SCAN01_KEYS_SetUiState(SCAN01_UI_HOLD);

    expect_action(press(KEY_PTT), SCAN01_ACT_NONE, "HOLD: PTT press");
    expect_action(release(KEY_PTT), SCAN01_ACT_RESUME, "HOLD: PTT short → RESUME");
    expect_action(press(KEY_PTT), SCAN01_ACT_NONE, "HOLD: PTT press (2)");
    ticks(80);
    expect_action(release(KEY_PTT), SCAN01_ACT_CAPTURE, "HOLD: PTT long → CAPTURE");

    expect_action(press(KEY_UP), SCAN01_ACT_NAV_UP, "HOLD: UP → prev car");
    expect_action(press(KEY_DOWN), SCAN01_ACT_NAV_DOWN, "HOLD: DOWN → next car");
    expect_action(press(KEY_STAR), SCAN01_ACT_SCAN, "HOLD: * short → SCAN");
    expect_action(held(KEY_STAR), SCAN01_ACT_LOCKOUT, "HOLD: * held → lockout");
    expect_action(press(KEY_EXIT), SCAN01_ACT_SCAN, "HOLD: EXIT → SCAN");
    expect_action(held(KEY_EXIT), SCAN01_ACT_HOME, "HOLD: EXIT held → HOME");
    expect_action(held(KEY_0), SCAN01_ACT_BRD, "HOLD: 0 held → BRD");
    expect_action(held(KEY_5), SCAN01_ACT_WX, "HOLD: 5 held → WX");
    expect_action(held(KEY_9), SCAN01_ACT_MYDRIVER, "HOLD: 9 held → my driver");
    expect_action(press(KEY_F), SCAN01_ACT_FAVORITES, "HOLD: # → favorites");
}

static void test_list_matrix(void)
{
    reset();
    SCAN01_KEYS_SetUiState(SCAN01_UI_LIST);

    expect_action(press(KEY_PTT), SCAN01_ACT_NONE, "LIST: PTT press");
    expect_action(release(KEY_PTT), SCAN01_ACT_RESUME, "LIST: PTT short → RESUME");
    expect_action(press(KEY_PTT), SCAN01_ACT_NONE, "LIST: PTT press (2)");
    ticks(80);
    expect_action(release(KEY_PTT), SCAN01_ACT_CAPTURE, "LIST: PTT long → CAPTURE");
    expect_action(press(KEY_UP), SCAN01_ACT_NAV_UP, "LIST: UP scrolls");
    expect_action(press(KEY_DOWN), SCAN01_ACT_NAV_DOWN, "LIST: DOWN scrolls");
    expect_action(held(KEY_UP), SCAN01_ACT_NAV_UP, "LIST: UP held repeats");
    expect_action(press(KEY_STAR), SCAN01_ACT_SCAN, "LIST: * short → SCAN");
    expect_action(held(KEY_STAR), SCAN01_ACT_LOCKOUT, "LIST: * held → lockout selected");
    expect_action(press(KEY_EXIT), SCAN01_ACT_SCAN, "LIST: EXIT → SCAN");
    expect_action(held(KEY_EXIT), SCAN01_ACT_HOME, "LIST: EXIT held → HOME");
    expect_action(press(KEY_MENU), SCAN01_ACT_SETUP, "LIST: M → SETUP");
    expect_action(press(KEY_SIDE2), SCAN01_ACT_GROUP, "LIST: F2 → group cycle");
    expect_action(press(KEY_F), SCAN01_ACT_FAVORITES, "LIST: # → favorites");
}

static void test_setup_matrix(void)
{
    reset();
    SCAN01_KEYS_SetUiState(SCAN01_UI_SETUP);

    expect_action(press(KEY_PTT), SCAN01_ACT_BACK, "SETUP: PTT → back");
    expect_action(release(KEY_PTT), SCAN01_ACT_NONE, "SETUP: PTT release consumed");
    expect_action(press(KEY_EXIT), SCAN01_ACT_BACK, "SETUP: EXIT → back");
    expect_action(held(KEY_EXIT), SCAN01_ACT_HOME, "SETUP: EXIT held → HOME");
    expect_action(press(KEY_MENU), SCAN01_ACT_BACK, "SETUP: M → back");
    expect_action(press(KEY_UP), SCAN01_ACT_NAV_UP, "SETUP: UP → value");
    expect_action(press(KEY_DOWN), SCAN01_ACT_NAV_DOWN, "SETUP: DOWN → value");
    expect_action(press(KEY_2), SCAN01_ACT_NONE, "SETUP: digits → nothing");
    expect_action(held(KEY_0), SCAN01_ACT_NONE, "SETUP: 0 held → nothing");
    expect_action(held(KEY_5), SCAN01_ACT_NONE, "SETUP: 5 held → nothing");
    expect_action(press(KEY_F), SCAN01_ACT_NONE, "SETUP: # → nothing");
    expect_action(press(KEY_SIDE1), SCAN01_ACT_NONE, "SETUP: F1 → nothing");
    expect_action(press(KEY_STAR), SCAN01_ACT_SCAN, "SETUP: * → SCAN");
    /* long-PTT timer must not arm in SETUP */
    expect_action(press(KEY_PTT), SCAN01_ACT_BACK, "SETUP: PTT again → back");
    expect_action(release(KEY_PTT), SCAN01_ACT_NONE, "SETUP: PTT release (2)");
}

static void test_brd_wx_matrix(void)
{
    reset();
    SCAN01_KEYS_SetUiState(SCAN01_UI_BRD);

    expect_action(press(KEY_PTT), SCAN01_ACT_NONE, "BRD: PTT press");
    expect_action(release(KEY_PTT), SCAN01_ACT_RESUME, "BRD: PTT → RESUME");
    expect_action(press(KEY_UP), SCAN01_ACT_NAV_UP, "BRD: UP → presets");
    expect_action(press(KEY_STAR), SCAN01_ACT_SCAN, "BRD: * → SCAN");
    expect_action(held(KEY_STAR), SCAN01_ACT_NONE, "BRD: * held → nothing (no lockout)");
    expect_action(press(KEY_EXIT), SCAN01_ACT_SCAN, "BRD: EXIT → SCAN");
    expect_action(held(KEY_5), SCAN01_ACT_WX, "BRD: 5 held → WX (nested)");
    expect_action(press(KEY_2), SCAN01_ACT_TYPE_UPDATE, "BRD: digits tune");

    reset();
    SCAN01_KEYS_SetUiState(SCAN01_UI_WX);
    expect_action(press(KEY_PTT), SCAN01_ACT_NONE, "WX: PTT press");
    expect_action(release(KEY_PTT), SCAN01_ACT_RESUME, "WX: PTT → RESUME");
    expect_action(press(KEY_DOWN), SCAN01_ACT_NAV_DOWN, "WX: DOWN → channels");
    expect_action(press(KEY_STAR), SCAN01_ACT_SCAN, "WX: * → SCAN");
    expect_action(held(KEY_0), SCAN01_ACT_BRD, "WX: 0 held → BRD (nested)");
    expect_action(held(KEY_9), SCAN01_ACT_MYDRIVER, "WX: 9 held → my driver");
    expect_action(press(KEY_2), SCAN01_ACT_NONE, "WX: digits → nothing (never configures)");
}

static void test_capture_matrix(void)
{
    reset();
    SCAN01_KEYS_SetUiState(SCAN01_UI_CAPTURE);
    SCAN01_TYPE_SetAutoCommit(false);

    expect_action(press(KEY_PTT), SCAN01_ACT_SAVE, "CAPTURE: PTT → SAVE");
    expect_action(release(KEY_PTT), SCAN01_ACT_NONE, "CAPTURE: PTT release consumed");
    expect_action(press(KEY_2), SCAN01_ACT_TYPE_UPDATE, "CAPTURE: digit into number field");
    expect_action(press(KEY_4), SCAN01_ACT_TYPE_UPDATE, "CAPTURE: second digit");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "24") == 0, "CAPTURE: buffer '24'");
    /* no auto-commit in CAPTURE — the form stays until PTT saves or EXIT cancels */
    ticks(1000);
    expect(SCAN01_KEYS_IsTyping(), "CAPTURE: no timeout commit");
    char entry[16];
    expect(!SCAN01_KEYS_PollCommit(entry, sizeof(entry)), "CAPTURE: nothing committed");
    expect_action(press(KEY_PTT), SCAN01_ACT_SAVE, "CAPTURE: PTT saves again");
    expect_action(release(KEY_PTT), SCAN01_ACT_NONE, "CAPTURE: release consumed (2)");
    expect_action(press(KEY_EXIT), SCAN01_ACT_TYPE_UPDATE, "CAPTURE: EXIT deletes");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "2") == 0, "CAPTURE: buffer '2' after delete");
    expect_action(held(KEY_EXIT), SCAN01_ACT_NONE, "CAPTURE: EXIT held clears");
    expect(!SCAN01_KEYS_IsTyping(), "CAPTURE: cleared");
}

/* ---- typing (vision §4.3) ---- */

static void test_typing_number(void)
{
    reset();
    type_key('2');
    type_key('4');
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "24") == 0, "TYPING: '24'");
    /* timeout commits */
    ticks(150);
    expect(!SCAN01_KEYS_IsTyping(), "TYPING: committed after 1.5 s");
    char entry[16];
    expect(SCAN01_KEYS_PollCommit(entry, sizeof(entry)), "TYPING: commit pending");
    expect(strcmp(entry, "24") == 0, "TYPING: entry '24'");
    expect(!SCAN01_KEYS_PollCommit(entry, sizeof(entry)), "TYPING: commit drained");

    /* suffix letters: ▲ = A, ▲ = B, ▼ wraps */
    reset();
    type_key('2');
    type_key('9');
    expect_action(press(KEY_UP), SCAN01_ACT_TYPE_UPDATE, "TYPING: ▲ adds suffix");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "29A") == 0, "TYPING: '29A'");
    expect_action(press(KEY_UP), SCAN01_ACT_TYPE_UPDATE, "TYPING: ▲ cycles");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "29B") == 0, "TYPING: '29B'");
    expect_action(press(KEY_DOWN), SCAN01_ACT_TYPE_UPDATE, "TYPING: ▼ cycles back");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "29A") == 0, "TYPING: '29A' back");
    expect_action(press(KEY_DOWN), SCAN01_ACT_TYPE_UPDATE, "TYPING: ▼ wraps");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "29Z") == 0, "TYPING: '29Z' wrap");
    /* digit after suffix is refused (max 3 chars) */
    type_key('7');
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "29Z") == 0, "TYPING: no 4th char");
    /* digit after a SHORT suffix is refused too */
    reset();
    type_key('2');
    expect_action(press(KEY_UP), SCAN01_ACT_TYPE_UPDATE, "TYPING: ▲ adds suffix (2)");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "2A") == 0, "TYPING: '2A'");
    type_key('7');
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "2A") == 0, "TYPING: no digit after suffix");
    /* suffix on 3 digits refused */
    reset();
    type_key('1');
    type_key('0');
    type_key('0');
    expect_action(press(KEY_UP), SCAN01_ACT_NONE, "TYPING: ▲ on full number is a no-op");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "100") == 0, "TYPING: '100' stays (suffix needs room)");

    /* 4th digit refused in number mode */
    reset();
    type_key('2');
    type_key('4');
    type_key('5');
    type_key('6');
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "245") == 0, "TYPING: 4th digit refused");

    /* EXIT deletes; empty → typing ends */
    reset();
    type_key('1');
    type_key('3');
    expect_action(press(KEY_EXIT), SCAN01_ACT_TYPE_UPDATE, "TYPING: EXIT deletes");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "1") == 0, "TYPING: '1' after delete");
    expect_action(press(KEY_EXIT), SCAN01_ACT_TYPE_UPDATE, "TYPING: EXIT deletes (2)");
    expect(!SCAN01_KEYS_IsTyping(), "TYPING: empty → typing ends");
}

static void test_typing_freq(void)
{
    reset();
    type_key('4');
    type_key('5');
    type_key('1');
    expect_action(press(KEY_STAR), SCAN01_ACT_TYPE_UPDATE, "FREQ: * = point");
    expect(SCAN01_TYPE_IsFreq(), "FREQ: is freq");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "451.") == 0, "FREQ: '451.'");
    type_key('1');
    type_key('1');
    type_key('2');
    type_key('5');
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "451.1125") == 0, "FREQ: '451.1125'");
    ticks(150);
    char entry[16];
    expect(SCAN01_KEYS_PollCommit(entry, sizeof(entry)), "FREQ: committed");
    expect(strcmp(entry, "451.1125") == 0, "FREQ: entry '451.1125'");

    /* second point refused */
    reset();
    type_key('8');
    type_key('8');
    expect_action(press(KEY_STAR), SCAN01_ACT_TYPE_UPDATE, "FREQ: point (2)");
    expect_action(press(KEY_STAR), SCAN01_ACT_TYPE_UPDATE, "FREQ: second point ignored");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "88.") == 0, "FREQ: one point only");

    /* int part limited to 3; frac to 5 */
    reset();
    type_key('4');
    type_key('7');
    type_key('0');
    type_key('0');   /* 4th int digit refused */
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "470") == 0, "FREQ: int part ≤ 3");
    expect_action(press(KEY_STAR), SCAN01_ACT_TYPE_UPDATE, "FREQ: point (3)");
    type_key('0');
    type_key('0');
    type_key('0');
    type_key('0');
    type_key('0');   /* 5 frac digits */
    type_key('9');   /* 6th refused */
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "470.00000") == 0, "FREQ: frac part ≤ 5");

    /* no suffix in freq mode */
    expect_action(press(KEY_UP), SCAN01_ACT_NONE, "FREQ: ▲ in freq → nothing");

    /* deleting the point returns to number mode */
    reset();
    type_key('8');
    type_key('8');
    expect_action(press(KEY_STAR), SCAN01_ACT_TYPE_UPDATE, "FREQ: point (4)");
    expect_action(press(KEY_EXIT), SCAN01_ACT_TYPE_UPDATE, "FREQ: EXIT deletes point");
    expect(!SCAN01_TYPE_IsFreq(), "FREQ: back to number mode");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "88") == 0, "FREQ: '88'");

    /* point after a suffix letter is refused */
    reset();
    type_key('2');
    type_key('9');
    expect_action(press(KEY_UP), SCAN01_ACT_TYPE_UPDATE, "FREQ: ▲ adds suffix");
    expect_action(press(KEY_STAR), SCAN01_ACT_TYPE_UPDATE, "FREQ: * after suffix ignored");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "29A") == 0, "FREQ: '29A' stays");
    expect(!SCAN01_TYPE_IsFreq(), "FREQ: not a frequency");
}

static void test_typing_conflicts(void)
{
    /* held digit while typing a longer entry → suppressed (no trap) */
    reset();
    type_key('4');
    type_key('0');
    expect_action(held(KEY_0), SCAN01_ACT_NONE, "CONFLICT: held 0 mid-entry suppressed");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "40") == 0, "CONFLICT: entry intact");

    /* clean hold from idle: '0' alone + held → BRD (the label works) */
    reset();
    expect_action(press(KEY_0), SCAN01_ACT_TYPE_UPDATE, "CONFLICT: '0' starts typing");
    expect_action(held(KEY_0), SCAN01_ACT_BRD, "CONFLICT: clean held 0 → BRD");

    /* held * while typing → suppressed (point is the only use) */
    reset();
    type_key('2');
    expect_action(held(KEY_STAR), SCAN01_ACT_NONE, "CONFLICT: held * mid-entry suppressed");

    /* long-EXIT while typing clears; second long-EXIT is HOME */
    reset();
    type_key('7');
    type_key('7');
    expect_action(held(KEY_EXIT), SCAN01_ACT_NONE, "CONFLICT: long-EXIT clears");
    expect(!SCAN01_KEYS_IsTyping(), "CONFLICT: typing ended");
    expect_action(held(KEY_EXIT), SCAN01_ACT_HOME, "CONFLICT: next long-EXIT → HOME");

    /* M / F2 / # abandon the entry and act */
    reset();
    type_key('3');
    type_key('3');
    expect_action(press(KEY_MENU), SCAN01_ACT_SETUP, "CONFLICT: M abandons entry → SETUP");
    reset();
    type_key('3');
    expect_action(press(KEY_SIDE2), SCAN01_ACT_GROUP, "CONFLICT: F2 abandons entry → GROUP");
    reset();
    type_key('3');
    expect_action(press(KEY_F), SCAN01_ACT_FAVORITES, "CONFLICT: # abandons entry → favorites");

    /* F1 mid-entry is harmless: mute keeps the entry */
    reset();
    type_key('3');
    expect_action(press(KEY_SIDE1), SCAN01_ACT_MUTE, "CONFLICT: F1 mid-entry → MUTE");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "3") == 0, "CONFLICT: entry kept after MUTE");

    /* PTT cancels typing and acts (PTT wins) */
    reset();
    type_key('2');
    type_key('4');
    expect_action(press(KEY_PTT), SCAN01_ACT_NONE, "CONFLICT: PTT press mid-entry");
    expect_action(release(KEY_PTT), SCAN01_ACT_HOLD, "CONFLICT: PTT short cancels entry → HOLD");
    expect(!SCAN01_KEYS_IsTyping(), "CONFLICT: entry cancelled");

    /* long-PTT mid-entry → CAPTURE, entry cancelled */
    reset();
    type_key('2');
    type_key('4');
    expect_action(press(KEY_PTT), SCAN01_ACT_NONE, "CONFLICT: PTT press (2)");
    ticks(80);
    expect_action(release(KEY_PTT), SCAN01_ACT_CAPTURE, "CONFLICT: PTT long mid-entry → CAPTURE");
    expect(!SCAN01_KEYS_IsTyping(), "CONFLICT: entry cancelled (2)");

    /* held UP repeats the suffix cycle */
    reset();
    type_key('2');
    expect_action(held(KEY_UP), SCAN01_ACT_TYPE_UPDATE, "CONFLICT: held ▲ adds suffix");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "2A") == 0, "CONFLICT: '2A'");
    expect_action(held(KEY_UP), SCAN01_ACT_TYPE_UPDATE, "CONFLICT: held ▲ cycles");
    expect(strcmp(SCAN01_TYPE_GetBuffer(), "2B") == 0, "CONFLICT: '2B'");
}

static void test_state_transitions(void)
{
    /* entering a non-listening state aborts a pending entry and hold timer */
    reset();
    type_key('9');
    expect_action(press(KEY_MENU), SCAN01_ACT_SETUP, "STATE: M mid-entry");
    expect(!SCAN01_KEYS_IsTyping(), "STATE: entry cleared on SETUP");

    /* entering CAPTURE clears any pending entry */
    reset();
    type_key('5');
    SCAN01_KEYS_SetUiState(SCAN01_UI_CAPTURE);
    expect(!SCAN01_KEYS_IsTyping(), "STATE: entry cleared on CAPTURE");

    /* a stale PTT hold cannot survive a state change */
    reset();
    expect_action(press(KEY_PTT), SCAN01_ACT_NONE, "STATE: PTT press");
    SCAN01_KEYS_SetUiState(SCAN01_UI_SETUP);
    ticks(200);
    expect_action(press(KEY_PTT), SCAN01_ACT_BACK, "STATE: PTT in SETUP → BACK (no CAPTURE)");
    expect_action(release(KEY_PTT), SCAN01_ACT_NONE, "STATE: release consumed");

    /* a PTT release in a different state than its press is consumed:
     * CAPTURE SAVE acts on press and switches to SCAN — the release must
     * not re-read as HOLD */
    reset();
    SCAN01_KEYS_SetUiState(SCAN01_UI_CAPTURE);
    SCAN01_TYPE_SetAutoCommit(false);
    expect_action(press(KEY_PTT), SCAN01_ACT_SAVE, "STATE: CAPTURE PTT press → SAVE");
    SCAN01_KEYS_SetUiState(SCAN01_UI_SCAN);     /* the UI switched state on save */
    expect_action(release(KEY_PTT), SCAN01_ACT_NONE, "STATE: release consumed after state change");

    /* leaving CAPTURE restores auto-commit (CAPTURE is the only off-switch) */
    reset();
    SCAN01_KEYS_SetUiState(SCAN01_UI_CAPTURE);
    SCAN01_TYPE_SetAutoCommit(false);
    SCAN01_KEYS_SetUiState(SCAN01_UI_SCAN);
    type_key('7');
    ticks(150);
    char entry[16];
    expect(SCAN01_KEYS_PollCommit(entry, sizeof(entry)), "STATE: auto-commit restored after CAPTURE");
}

int main(void)
{
    test_scan_matrix();
    test_hold_matrix();
    test_list_matrix();
    test_setup_matrix();
    test_brd_wx_matrix();
    test_capture_matrix();
    test_typing_number();
    test_typing_freq();
    test_typing_conflicts();
    test_state_transitions();

    printf("key layer: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
