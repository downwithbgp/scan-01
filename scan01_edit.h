/* Scan 01 — multi-tap name editor (task T6b, vision §4.5)
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
 * Pure logic, host-tested in tests/test_edit.c. The base has no multi-tap
 * input (its input box is digits-only) — this is the new component.
 */

#ifndef SCAN01_EDIT_H
#define SCAN01_EDIT_H

#include <stdbool.h>

#include "driver/keyboard.h"

#define SCAN01_EDIT_MAX 10   /* the pack name field ("LAST INITIAL") */

void        SCAN01_EDIT_Reset(void);
bool        SCAN01_EDIT_ProcessKey(KEY_Code_t key); /* 2-9 letters, 0 = space; false = not a letter key */
void        SCAN01_EDIT_Tick10ms(void);             /* the same-key window */
void        SCAN01_EDIT_Delete(void);               /* remove the last character */
void        SCAN01_EDIT_Clear(void);
const char *SCAN01_EDIT_GetBuffer(void);

#endif /* SCAN01_EDIT_H */
