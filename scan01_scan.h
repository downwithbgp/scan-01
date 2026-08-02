/* Scan 01 — the scan engine (spec/p2-scan-engine: S1 universe, S2 timing,
 * S4 FOLLOW, S5 CSQ guard)
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
 * Pure logic: no firmware globals, no hardware — the UI feeds the 10 ms tick
 * with the BK4819's signals (squelch_open, tone_ok) and acts on the events
 * (EV_ENTRY -> tune the channel). Host-tested in tests/test_scan.c; the
 * headless radio drives it for real in the sim.
 */

#ifndef SCAN01_SCAN_H
#define SCAN01_SCAN_H

#include <stdbool.h>
#include <stdint.h>

/* ---- timing (10 ms ticks; P0-track-validation items, not UI settings) ---- */
#define SCAN_DWELL_10MS          8     /* 80 ms — base floor ~60 ms (chFrScanner.c:361) */
#define SCAN_DECODE_HOLD_10MS    20    /* 200 ms — the tone gate decides */
#define SCAN_UNMUTE_DEBOUNCE_10MS 4    /* 40 ms — skip the burst edge */
#define SCAN_HANG_10MS           25    /* 250 ms — syllable gaps within an exchange */
#define SCAN_CSQ_GUARD_10MS      500   /* 5 s — an open mic cannot hold the scan */
#define SCAN_FOLLOW_INTERLEAVE   8     /* the my-driver entry is revisited every 8 entries */

#define SCAN_UNIVERSE_MAX        89    /* 88 entries + the follow slot */

typedef enum {
    SCAN01_SCAN_IDLE = 0,       /* stopped (HOLD / sub-state / empty universe) */
    SCAN01_SCAN_WALK,           /* dwelling on the current entry */
    SCAN01_SCAN_DECODE,         /* candidate: tone-decode hold */
    SCAN01_SCAN_LANDED,         /* audio open on the current entry */
    SCAN01_SCAN_HANG,           /* carrier dropped; hang countdown */
} Scan01ScanState_t;

typedef enum {
    SCAN01_SCAN_EV_NONE = 0,
    SCAN01_SCAN_EV_ENTRY,       /* a new entry is current — tune it */
    SCAN01_SCAN_EV_LANDED,      /* the entry opened audio */
    SCAN01_SCAN_EV_HANG,        /* the carrier dropped — hang started */
    SCAN01_SCAN_EV_EMPTY,       /* the universe is empty */
} Scan01ScanEvent_t;

typedef enum {
    SCAN01_GROUP_ALL = 0,
    SCAN01_GROUP_A,
    SCAN01_GROUP_B,
    SCAN01_GROUP_C,
    SCAN01_GROUP_FAVS,
} Scan01Group_t;

typedef struct {
    uint16_t channel;           /* the base channel slot to tune */
    uint8_t  code_type;         /* PACK_CT_NONE / CTCSS / DCS — which tone flag gates */
    bool     follow;            /* the my-driver priority entry */
} Scan01ScanEntry_t;

void  SCAN01_SCAN_Init(void);

/* Rebuild the universe from the pack + the current group filter (spec §3).
 * follow_car_index = the my-driver car index, or 0xFF. Filter changes apply
 * at the next cycle boundary: the walk position is preserved. */
void  SCAN01_SCAN_Rebuild(uint8_t follow_car_index);

uint8_t       SCAN01_SCAN_Count(void);
Scan01Group_t SCAN01_SCAN_GetGroup(void);
void          SCAN01_SCAN_CycleGroup(void);      /* F2: ALL -> A -> B -> C -> FAVS -> ALL */

void SCAN01_SCAN_Start(void);
void SCAN01_SCAN_Stop(void);

Scan01ScanState_t    SCAN01_SCAN_GetState(void);
uint8_t              SCAN01_SCAN_GetPosition(void);
const Scan01ScanEntry_t *SCAN01_SCAN_GetCurrent(void);

/* 10 ms tick. squelch_open: the BK4819 squelch is open (candidate signal —
 * noise/RSSI based, tone-independent). tone_ok: the entry's code is present
 * (for tone'd entries; ignored for CSQ). Returns the events since the last
 * tick (the latest one). */
Scan01ScanEvent_t SCAN01_SCAN_Tick10ms(bool squelch_open, bool tone_ok);

/* Re-anchor the walk at a universe position (# / long-9). */
void SCAN01_SCAN_JumpTo(uint8_t position);

#endif /* SCAN01_SCAN_H */
