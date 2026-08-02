/* Scan 01 — multi-tap editor tests (task T6b, vision §4.5)
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
 */

#include <stdio.h>
#include <string.h>

#include "scan01_edit.h"

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

static void expect_buf(const char *want, const char *what)
{
    g_checks++;
    if (strcmp(SCAN01_EDIT_GetBuffer(), want) != 0) {
        g_failures++;
        printf("FAIL: %s — got '%s' want '%s'\n", what, SCAN01_EDIT_GetBuffer(), want);
    }
}

static void tick(int n)
{
    for (int i = 0; i < n; i++)
        SCAN01_EDIT_Tick10ms();
}

static void key(char digit)
{
    SCAN01_EDIT_ProcessKey(KEY_0 + (digit - '0'));
}

static void test_cycling(void)
{
    SCAN01_EDIT_Reset();
    key('2');
    expect_buf("A", "2 → A");
    key('2');
    expect_buf("B", "2,2 → B (cycle)");
    key('2');
    expect_buf("C", "2,2,2 → C");
    key('2');
    expect_buf("A", "4th press wraps → A");

    SCAN01_EDIT_Reset();
    key('9');
    expect_buf("W", "9 → W");
    key('9');
    expect_buf("X", "9,9 → X");
    key('9');
    expect_buf("Y", "9,9,9 → Y");
    key('9');
    expect_buf("Z", "9,9,9,9 → Z");
    key('9');
    expect_buf("W", "wraps → W");
}

static void test_new_letter_and_window(void)
{
    SCAN01_EDIT_Reset();
    key('2');
    key('3');
    expect_buf("AD", "2 then 3 → AD");
    key('2');
    expect_buf("ADA", "2 after 3 → ADA");

    /* the window: same key after expiry starts a new letter */
    SCAN01_EDIT_Reset();
    key('2');
    tick(149);
    key('2');
    expect_buf("B", "same key within 1.49 s still cycles");
    SCAN01_EDIT_Reset();
    key('2');
    tick(150);
    key('2');
    expect_buf("AA", "window expired → new letter");
}

static void test_space_and_delete(void)
{
    SCAN01_EDIT_Reset();
    key('4');
    key('4');
    key('4');
    expect_buf("I", "444 → I");
    key('0');
    expect_buf("I ", "0 → space");
    key('2');
    expect_buf("I A", "new letter after space");

    SCAN01_EDIT_Reset();
    key('2');
    SCAN01_EDIT_Delete();
    expect_buf("", "delete removes");
    key('5');
    key('5');
    expect_buf("K", "55 → K");
    SCAN01_EDIT_Delete();
    expect_buf("", "delete (2)");

    SCAN01_EDIT_Reset();
    key('6');
    key('6');
    expect_buf("N", "66 → N");
    SCAN01_EDIT_Clear();
    expect_buf("", "clear");
}

static void test_cap_and_ignored(void)
{
    SCAN01_EDIT_Reset();
    /* 10 chars: 2,2,2 (C) 3,3,3 (F) 4,4,4 (I) 5,5 (K) → "CFIK" 4 chars... use
     * distinct keys to fill: 2 3 4 5 6 7 8 9 2 3 = A D G J M P T W A D = 10 */
    key('2'); key('3'); key('4'); key('5'); key('6');
    key('7'); key('8'); key('9'); key('2'); key('3');
    expect_buf("ADGJMPTWAD", "10 chars");
    key('3');                                   /* same key at cap: cycles the pending letter */
    expect_buf("ADGJMPTWAE", "same-key cycle at cap");
    key('4');                                   /* different key at cap: consumed */
    expect_buf("ADGJMPTWAE", "new letter at cap refused");

    /* non-letter keys are not consumed */
    SCAN01_EDIT_Reset();
    expect(!SCAN01_EDIT_ProcessKey(KEY_EXIT), "EXIT not a letter key");
    expect(!SCAN01_EDIT_ProcessKey(KEY_STAR), "* not a letter key");
    expect(SCAN01_EDIT_ProcessKey(KEY_0), "0 IS a letter key (space)");
    expect_buf(" ", "0 → space");
}

int main(void)
{
    test_cycling();
    test_new_letter_and_window();
    test_space_and_delete();
    test_cap_and_ignored();

    printf("edit layer: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
