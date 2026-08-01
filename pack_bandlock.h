/* Scan 01 — pack band-lock (spec/p1-pack-eeprom §8, task T2)
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

#ifndef PACK_BANDLOCK_H
#define PACK_BANDLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PACK_MODE_RACE = 0,     // pack-world tuning: the entry-band set only
    PACK_MODE_PRACTICE = 1  // daily driver: entry bands + 2m ham
} PACK_Mode_t;

// Single point of enforcement for every tune path (BK4819 set, CAPTURE,
// BRD, WX, PRACTICE frequency entry/scanning). The entry-band set is valid
// in both modes; 2m is PRACTICE-only; US cellular ranges are hard-rejected
// in both modes, always (ECPA §2511) — the radio must never tune them.
bool PACK_FreqAllowed(uint32_t freq_hz, PACK_Mode_t mode);

#endif
