/* Scan 01 — the fading legend (lesson engine)
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
 * See scan01_lessons.h for the design. Timing (10 ms ticks; tuned for a
 * quiet state line — a hint appears only after 6 s of true idle and stays
 * 3 s, then the next unlearned lesson rotates in):
 */

#include "scan01_lessons.h"

#include <string.h>

#include "settings_pack.h"

#define LESSON_IDLE_10MS   600    /* 6 s idle before the first hint */
#define LESSON_SHOW_10MS   300    /* 3 s per hint */

/* The six lessons, hero first, ≤ 15 chars (the state-line cap). */
static const char *const HINTS[LESSON_COUNT] = {
    "PTT = HOLD",
    "HOLD 5 = WX",
    "HOLD 0 = FM",
    "HOLD 9 = CALL",
    "HOLD * = LOCK",
    "HOLD EXIT = HOME",
};

static uint8_t   g_learned;       /* pack bits, 1 = learned */
static uint8_t   g_cursor;        /* the lesson currently on offer */
static uint16_t  g_idle_10ms;
static uint16_t  g_show_10ms;
static bool      g_hinting;

static bool LessonForAction(Scan01Action_t action, Scan01Lesson_t *lesson)
{
    switch (action) {
    case SCAN01_ACT_HOLD:
    case SCAN01_ACT_RESUME:
        *lesson = LESSON_PTT_HOLD;
        return true;
    case SCAN01_ACT_WX:          *lesson = LESSON_HOLD5_WX;       return true;
    case SCAN01_ACT_BRD:         *lesson = LESSON_HOLD0_FM;       return true;
    case SCAN01_ACT_MYDRIVER:    *lesson = LESSON_HOLD9_CALL;     return true;
    case SCAN01_ACT_LOCKOUT:     *lesson = LESSON_HOLDSTAR_LOCK;  return true;
    case SCAN01_ACT_HOME:        *lesson = LESSON_HOLDEXIT_HOME;  return true;
    default:
        return false;
    }
}

/* Advance the cursor to the next unlearned lesson, wrapping. Stops the
 * rotation when nothing is left to teach. */
static void Advance(void)
{
    for (uint8_t i = 1; i <= LESSON_COUNT; i++) {
        uint8_t next = (uint8_t)((g_cursor + i) % LESSON_COUNT);
        if ((g_learned & LESSON_PACK_BIT(next)) == 0) {
            g_cursor = next;
            return;
        }
    }
    g_hinting = false;                      /* everything is learned */
}

/* Repoint the cursor at an unlearned lesson (the invariant: while hinting,
 * the cursor names a lesson that still needs teaching). */
static void SeekUnlearned(void)
{
    for (uint8_t i = 0; i < LESSON_COUNT; i++) {
        if ((g_learned & LESSON_PACK_BIT(g_cursor)) == 0)
            return;
        g_cursor = (uint8_t)((g_cursor + 1) % LESSON_COUNT);
    }
    g_hinting = false;                      /* everything is learned */
}

void SCAN01_LESSONS_Init(void)
{
    g_learned = PACK_GetLessons();
    g_cursor = 0;
    g_idle_10ms = 0;
    g_show_10ms = 0;
    g_hinting = false;
}

void SCAN01_LESSONS_KeyActivity(void)
{
    g_idle_10ms = 0;
    g_hinting = false;
    g_show_10ms = 0;
}

void SCAN01_LESSONS_MarkLearned(Scan01Action_t action)
{
    Scan01Lesson_t lesson;
    if (!LessonForAction(action, &lesson))
        return;
    uint8_t bit = LESSON_PACK_BIT(lesson);
    if (g_learned & bit)
        return;                             /* already learned */
    g_learned |= bit;
    PACK_SetLessons(g_learned);             /* persist (one EEPROM write per lesson) */
    if (g_hinting && g_cursor == lesson) {
        g_show_10ms = 0;
        Advance();                          /* move straight on to the next lesson */
    }
}

bool SCAN01_LESSONS_AllLearned(void)
{
    return g_learned == LESSON_PACK_ALL;
}

const char *SCAN01_LESSONS_CurrentHint(void)
{
    if (!g_hinting)
        return NULL;
    if (g_learned & LESSON_PACK_BIT(g_cursor))
        SeekUnlearned();                    /* the lesson was learned mid-show */
    if (!g_hinting)
        return NULL;
    return HINTS[g_cursor];
}

void SCAN01_LESSONS_Tick10ms(bool can_teach)
{
    if (can_teach && !SCAN01_LESSONS_AllLearned()) {
        if (g_hinting) {
            if (--g_show_10ms == 0) {
                g_show_10ms = LESSON_SHOW_10MS;
                Advance();
            }
        } else if (++g_idle_10ms >= LESSON_IDLE_10MS) {
            g_hinting = true;
            g_show_10ms = LESSON_SHOW_10MS;
            SeekUnlearned();                /* point at the first unlearned lesson */
        }
    } else {
        g_idle_10ms = 0;
        g_hinting = false;
    }
}
