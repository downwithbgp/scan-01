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

#include "pack_bandlock.h"

// The entry-band set (spec §3/§6) as a union for the filter:
//   88–108       FM broadcast
//   108–137      airband (AM only at the schema level)
//   144–148      2m ham — PRACTICE only
//   151–162      VHF dirt-track (151–160) + marine VHF (156–162)
//   162.4–162.55 NOAA
//   450–470      racing/teams, FRS/GMRS (462.55–467.725), business
// FRS is a subset of 450–470 and marine overlaps the dirt-track range, so
// the filter needs only the union; entry-kind semantics (AM-only airband,
// broadcast-path-only FM) live in the pack schema and the caller.
static const struct {
    uint32_t lo;
    uint32_t hi;
    bool     practice_only;
} BAND_TABLE[] = {
    {  88000000u,  108000000u, false },
    { 108000000u,  137000000u, false },
    { 144000000u,  148000000u, true  },
    { 151000000u,  162000000u, false },
    { 162400000u,  162550000u, false },
    { 450000000u,  470000000u, false },
};

// US cellular — receiving it is a felony (18 U.S.C. §2511 / ECPA).
// Hard-rejected in both modes, always, before anything else.
static const struct {
    uint32_t lo;
    uint32_t hi;
} CELLULAR_BANDS[] = {
    {  824000000u,  894000000u },
    { 1710000000u, 1755000000u },
    { 1850000000u, 1990000000u },
    { 2110000000u, 2155000000u },
};

bool PACK_FreqAllowed(uint32_t freq_hz, PACK_Mode_t mode)
{
    unsigned int i;

    for (i = 0; i < (sizeof(CELLULAR_BANDS) / sizeof(CELLULAR_BANDS[0])); i++) {
        if (freq_hz >= CELLULAR_BANDS[i].lo && freq_hz <= CELLULAR_BANDS[i].hi)
            return false;
    }

    for (i = 0; i < (sizeof(BAND_TABLE) / sizeof(BAND_TABLE[0])); i++) {
        if (freq_hz >= BAND_TABLE[i].lo && freq_hz <= BAND_TABLE[i].hi) {
            if (BAND_TABLE[i].practice_only && mode != PACK_MODE_PRACTICE)
                return false;
            return true;
        }
    }

    return false;
}
