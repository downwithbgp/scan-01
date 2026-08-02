/* Scan 01 — pack status over the PC UART (task T7, spec §7)
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

#include <string.h>

#include "external/printf/printf.h"
#include "pack_uart.h"
#include "settings_pack.h"

static void FormatFreq(char *out, uint32_t hz)
{
    uint32_t mhz = hz / 1000000u;
    uint32_t frac = hz % 1000000u;
    char buf[8];

    if (frac == 0) {
        sprintf(out, "%u", (unsigned)mhz);
        return;
    }
    sprintf(buf, "%06u", (unsigned)frac);
    int len = 6;
    while (len > 1 && buf[len - 1] == '0')
        len--;
    buf[len] = 0;
    sprintf(out, "%u.%s", (unsigned)mhz, buf);
}

uint16_t PACK_UART_BuildStatus(char *out, uint16_t max)
{
    uint16_t n = 0;
    char freq[16];

#define EMIT(...)                                                        \
    do {                                                                 \
        int r = snprintf(out + n, (size_t)(max - n), __VA_ARGS__);       \
        if (r < 0)                                                       \
            return n;                                                    \
        n += (uint16_t)r;                                                \
        if (n >= max)                                                    \
            return max;                                                  \
    } while (0)

    EMIT("SC01 v%u\n", (unsigned)PACK_VERSION);
    EMIT("cars=%u stations=%u\n", (unsigned)PACK_CarCount(),
         (unsigned)PACK_StationCount());
    EMIT("sealed=%u practice=%u\n", PACK_IsSealed() ? 1u : 0u,
         PACK_IsPractice() ? 1u : 0u);
    for (uint8_t i = 0; i < PACK_CarCount(); i++) {
        const PackCar_t *c = PACK_GetCar(i);
        if (c != NULL && c->origin == PACK_ORIGIN_CAPTURED) {
            FormatFreq(freq, c->freq_hz);
            EMIT("captured %s %s %s\n", c->number, freq, c->name);
        }
    }
#undef EMIT
    return n;
}
