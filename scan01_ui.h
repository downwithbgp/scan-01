/* Scan 01 — the display (task T6, vision §5)
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
 * One DISPLAY_SCAN01 owns every Scan 01 screen (SCAN / HOLD / LIST /
 * CAPTURE); the internal state machine drives the radio actions and the
 * key layer (scan01_keys.c). The base's screens (MAIN/MENU/...) are never
 * selected in this edition.
 */

#ifndef SCAN01_UI_H
#define SCAN01_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/keyboard.h"

void SCAN01_UI_Init(void);
void SCAN01_UI_ProcessKeys(KEY_Code_t key, bool bKeyPressed, bool bKeyHeld);
void SCAN01_UI_Tick10ms(void);
void UI_DisplayScan01(void);

#endif /* SCAN01_UI_H */
