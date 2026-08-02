/* Scan 01 — the scan engine implementation (spec/p2-scan-engine)
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

#include "scan01_scan.h"
#include "settings_pack.h"

static Scan01ScanEntry_t g_entries[SCAN_UNIVERSE_MAX];
static uint8_t           g_count;
static uint8_t           g_pos;            /* current universe position */
static uint8_t           g_follow_pos;     /* the follow slot, or 0xFF */
static uint8_t           g_steps;          /* entries walked since the follow visit */

static Scan01ScanState_t g_state;
static int16_t           g_timer;          /* the current state's countdown */

static Scan01Group_t     g_group;
static uint8_t           g_guard_pos;      /* CSQ-guard skip target, or 0xFF */
static uint16_t          g_landed_ticks;   /* CSQ landed duration */

static Scan01ScanEvent_t Advance(void);

void SCAN01_SCAN_Init(void)
{
    g_count = 0;
    g_pos = 0;
    g_follow_pos = 0xFF;
    g_steps = 0;
    g_state = SCAN01_SCAN_IDLE;
    g_timer = 0;
    g_group = SCAN01_GROUP_ALL;
    g_guard_pos = 0xFF;
    g_landed_ticks = 0;
}

uint8_t SCAN01_SCAN_Count(void) { return g_count; }

Scan01Group_t SCAN01_SCAN_GetGroup(void) { return g_group; }

void SCAN01_SCAN_CycleGroup(void)
{
    g_group = (Scan01Group_t)((g_group + 1) % 5);
}

const Scan01ScanEntry_t *SCAN01_SCAN_GetCurrent(void)
{
    return (g_count > 0) ? &g_entries[g_pos] : NULL;
}

uint8_t SCAN01_SCAN_GetPosition(void) { return g_pos; }

Scan01ScanState_t SCAN01_SCAN_GetState(void) { return g_state; }

/* ---- the universe builder (spec §3) ---- */

static bool CarLocked(uint8_t car_index)
{
    const uint8_t *bitmap = PACK_LockoutBitmap();
    return (bitmap[car_index >> 3] & (1u << (car_index & 7))) != 0;
}

static void AddEntry(uint16_t channel, uint8_t code_type, bool follow)
{
    if (g_count >= SCAN_UNIVERSE_MAX)
        return;
    g_entries[g_count].channel = channel;
    g_entries[g_count].code_type = code_type;
    g_entries[g_count].follow = follow;
    if (follow)
        g_follow_pos = g_count;
    g_count++;
}

void SCAN01_SCAN_Rebuild(uint8_t follow_car_index)
{
    uint8_t old_pos = g_pos;
    uint8_t old_count = g_count;

    g_count = 0;
    g_follow_pos = 0xFF;

    /* cars in pack order (compose already orders venue 0 then venue 1) */
    for (uint8_t i = 0; i < PACK_CarCount(); i++) {
        const PackCar_t *car = PACK_GetCar(i);
        bool in_filter;

        if (car == NULL || CarLocked(i))
            continue;
        switch (g_group) {
        case SCAN01_GROUP_ALL:
            in_filter = true;
            break;
        case SCAN01_GROUP_A:
        case SCAN01_GROUP_B:
        case SCAN01_GROUP_C:
            in_filter = (car->group == (uint8_t)(g_group - SCAN01_GROUP_A));
            break;
        default: /* FAVS: favorites + my-driver is a favorite by definition */
            in_filter = car->favorite || (i == follow_car_index);
            break;
        }
        if (!in_filter)
            continue;
        AddEntry(car->channel, car->code_type, i == follow_car_index);
    }

    /* non-broadcast, non-digital stations — always scanned except in FAVS */
    if (g_group != SCAN01_GROUP_FAVS) {
        for (uint8_t i = 0; i < PACK_StationCount(); i++) {
            const PackStation_t *st = PACK_GetStation(i);
            if (st == NULL || st->kind == PACK_KIND_BROADCAST || st->digital)
                continue;
            AddEntry(st->channel, st->tone_is_dcs ? PACK_CT_DCS : PACK_CT_NONE, false);
        }
    }

    /* FOLLOW: the my-driver car is always in the walk, even outside the filter */
    if (follow_car_index != 0xFF && g_follow_pos == 0xFF) {
        const PackCar_t *car = PACK_GetCar(follow_car_index);
        if (car != NULL)
            AddEntry(car->channel, car->code_type, true);
    }

    /* preserve the walk position across a filter change (cycle boundary);
     * an empty universe is the only way the position can go stale */
    if (g_count > 0 && old_count > 0 && old_pos < g_count)
        g_pos = old_pos;
    else
        g_pos = 0;
}

/* ---- the walk (spec §4.1, §5) ---- */

static uint8_t NextPosition(void)
{
    if (g_count == 0)
        return 0;
    /* FOLLOW: every 8 walked entries, revisit the priority slot */
    if (g_follow_pos != 0xFF && g_steps >= SCAN_FOLLOW_INTERLEAVE)
        return g_follow_pos;
    return (uint8_t)((g_pos + 1) % g_count);
}

/* the CSQ guard: an entry that held the scan > 5 s is skipped on its next
 * visit (spec §5) */
static uint8_t GuardedNext(uint8_t next)
{
    if (g_guard_pos == next && g_guard_pos != 0xFF && g_count > 1) {
        g_guard_pos = 0xFF;
        next = (uint8_t)((next + 1) % g_count);
    }
    return next;
}

static Scan01ScanEvent_t Advance(void)
{
    if (g_count == 0) {
        g_state = SCAN01_SCAN_IDLE;
        return SCAN01_SCAN_EV_EMPTY;
    }
    uint8_t next = GuardedNext(NextPosition());
    g_pos = next;
    if (g_entries[next].follow) {
        g_steps = 0;                        /* the priority slot resets the interleave */
    } else {
        g_steps++;
    }
    g_state = SCAN01_SCAN_WALK;
    g_timer = SCAN_DWELL_10MS;
    return SCAN01_SCAN_EV_ENTRY;
}

void SCAN01_SCAN_Start(void)
{
    g_state = SCAN01_SCAN_WALK;
    g_timer = SCAN_DWELL_10MS;
    g_steps = 0;
}

void SCAN01_SCAN_Stop(void)
{
    g_state = SCAN01_SCAN_IDLE;
}

void SCAN01_SCAN_JumpTo(uint8_t position)
{
    if (position >= g_count)
        return;
    g_pos = position;
    g_state = SCAN01_SCAN_WALK;
    g_timer = SCAN_DWELL_10MS;
    if (g_entries[position].follow)
        g_steps = 0;
    else
        g_steps = 1;                        /* the re-anchored entry counts as walked */
    g_guard_pos = 0xFF;
}

/* ---- the 10 ms tick (spec §4) ---- */

Scan01ScanEvent_t SCAN01_SCAN_Tick10ms(bool squelch_open, bool tone_ok)
{
    Scan01ScanEvent_t ev = SCAN01_SCAN_EV_NONE;
    const Scan01ScanEntry_t *entry;

    switch (g_state) {
    case SCAN01_SCAN_IDLE:
        break;

    case SCAN01_SCAN_WALK:
        if (squelch_open) {
            /* candidate: pause for the tone-decode hold (spec §4.2) */
            g_state = SCAN01_SCAN_DECODE;
            g_timer = SCAN_DECODE_HOLD_10MS;
        } else if (--g_timer <= 0) {
            ev = Advance();
        }
        break;

    case SCAN01_SCAN_DECODE:
        if (!squelch_open) {
            /* a false candidate — the burst died before the gate could decide */
            g_timer = 0;
            ev = Advance();
        } else if (--g_timer <= 0) {
            entry = &g_entries[g_pos];
            bool gate = (entry->code_type == PACK_CT_NONE) ? true : tone_ok;
            if (gate) {
                g_state = SCAN01_SCAN_LANDED;
                g_landed_ticks = 0;
                ev = SCAN01_SCAN_EV_LANDED;
            } else {
                /* a foreign tone on the entry's frequency — not our car, move on */
                ev = Advance();
            }
        }
        break;

    case SCAN01_SCAN_LANDED:
        entry = &g_entries[g_pos];
        {
            bool keep = (entry->code_type == PACK_CT_NONE)
                            ? squelch_open
                            : (squelch_open && tone_ok);
            if (!keep) {
                g_state = SCAN01_SCAN_HANG;
                g_timer = SCAN_HANG_10MS;
                ev = SCAN01_SCAN_EV_HANG;
            } else if (entry->code_type == PACK_CT_NONE) {
                /* the CSQ hang guard: 5 s of continuous open-mic -> skip once */
                if (++g_landed_ticks >= SCAN_CSQ_GUARD_10MS) {
                    g_guard_pos = g_pos;
                    g_state = SCAN01_SCAN_HANG;
                    g_timer = 0;            /* no hang — straight back to the walk */
                    ev = SCAN01_SCAN_EV_HANG;
                }
            }
        }
        break;

    case SCAN01_SCAN_HANG:
        entry = &g_entries[g_pos];
        if (squelch_open && (entry->code_type == PACK_CT_NONE || tone_ok)) {
            /* the next syllable of the same exchange — no chopping (spec §4.3) */
            g_state = SCAN01_SCAN_LANDED;
            g_landed_ticks = 0;
            ev = SCAN01_SCAN_EV_LANDED;
        } else if (--g_timer <= 0) {
            ev = Advance();
        }
        break;
    }
    return ev;
}
