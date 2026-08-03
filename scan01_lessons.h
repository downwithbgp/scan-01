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
 * The radio teaches itself, then goes permanently quiet. While the user
 * idles in SCAN and lessons remain unlearned, the state line cycles one
 * hint at a time ("HOLD 5 = WX", "PTT = HOLD", ...); the moment the user
 * performs the gesture, the key layer's action marks that lesson learned
 * (persisted in the pack header flags, settings_pack.c). When every
 * lesson is learned — or a real pack arrives with its lessons all-learned
 * (packtool default) — the legend fades forever: Rams 4 (the device
 * explains itself) and Rams 10 (as little design as possible) agree.
 *
 * Pure logic: no hardware, no globals beyond module state. The UI feeds
 * Tick10ms(can_teach) and observes actions. Host-tested in test_lessons.c.
 */

#ifndef SCAN01_LESSONS_H
#define SCAN01_LESSONS_H

#include <stdbool.h>
#include <stdint.h>

#include "scan01_keys.h"

typedef enum {
    LESSON_PTT_HOLD = 0,    /* PTT short: hold the car / resume */
    LESSON_HOLD5_WX,        /* hold 5 (printed NOAA) = weather */
    LESSON_HOLD0_FM,        /* hold 0 (printed FM) = broadcast radio */
    LESSON_HOLD9_CALL,      /* hold 9 (printed CALL) = my driver */
    LESSON_HOLDSTAR_LOCK,   /* hold * in HOLD/LIST = lock the car out */
    LESSON_HOLDEXIT_HOME,   /* long-EXIT = HOME, the car browser */
    LESSON_COUNT,
} Scan01Lesson_t;

/* The pack-header representation: 1 = learned, bit (lesson + 1). */
#define LESSON_PACK_BIT(lesson) (uint8_t)(1u << ((lesson) + 1))
#define LESSON_PACK_ALL        (uint8_t)(((1u << LESSON_COUNT) - 1) << 1) /* 0x7E */

void         SCAN01_LESSONS_Init(void);        /* read the pack's lesson flags */
void         SCAN01_LESSONS_KeyActivity(void); /* any key event: idle resets, hint hides */
void         SCAN01_LESSONS_MarkLearned(Scan01Action_t action);
bool         SCAN01_LESSONS_AllLearned(void);
const char  *SCAN01_LESSONS_CurrentHint(void); /* NULL = silent */
void         SCAN01_LESSONS_Tick10ms(bool can_teach);

#endif /* SCAN01_LESSONS_H */
