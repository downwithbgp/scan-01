/* Scan 01 — scan engine tests (spec/p2-scan-engine S1/S2/S4/S5)
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

#include "scan01_scan.h"
#include "settings_pack.h"

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

static void expect_state(Scan01ScanState_t got, Scan01ScanState_t want, const char *what)
{
    g_checks++;
    if (got != want) {
        g_failures++;
        printf("FAIL: %s — state %d want %d\n", what, (int)got, (int)want);
    }
}

static void expect_pos(uint8_t got, uint8_t want, const char *what)
{
    g_checks++;
    if (got != want) {
        g_failures++;
        printf("FAIL: %s — pos %u want %u\n", what, got, want);
    }
}

/* ---- the pack fixture: 8 cars (mixed groups/venues/favorites/lockouts)
 *      + a broadcast station (excluded) + a digital station (excluded) ---- */

static uint8_t g_eeprom[0x2000];
void EEPROM_ReadBuffer(uint16_t Address, void *pBuffer, uint8_t Size) { memcpy(pBuffer, g_eeprom + Address, Size); }
void EEPROM_WriteBuffer(uint16_t Address, const void *pBuffer) { memcpy(g_eeprom + Address, pBuffer, 8); }

static PackCar_t make_car(const char *num, uint32_t hz, uint8_t group, bool fav, uint8_t ct)
{
    PackCar_t c;
    memset(&c, 0, sizeof(c));
    strncpy(c.number, num, 3);
    c.freq_hz = hz;
    c.group = group;
    c.favorite = fav;
    c.code_type = ct;
    c.narrow = true;
    return c;
}

static void add_car(const char *num, uint32_t hz, uint8_t group, bool fav, uint8_t ct)
{
    PackCar_t c = make_car(num, hz, group, fav, ct);
    if (!PACK_AddCapture(&c))
        printf("fixture: add_car %s FAILED\n", num);
}

static void boot(void)
{
    memset(g_eeprom, 0xFF, sizeof(g_eeprom));
    memset(g_eeprom, 0, 0x50);
    PACK_Init();
    SCAN01_SCAN_Init();
}

static void tick(int n, bool sq, bool tone)
{
    for (int i = 0; i < n; i++)
        SCAN01_SCAN_Tick10ms(sq, tone);
}

/* ---- the universe (S1) ---- */

static void test_universe(void)
{
    boot();
    add_car("24", 450887500u, 0, true, PACK_CT_CTCSS);   /* group A, fav */
    add_car("29A", 451125000u, 1, false, PACK_CT_NONE);
    add_car("3", 452100000u, 2, false, PACK_CT_CTCSS);   /* group C */
    add_car("48", 451112500u, 0, false, PACK_CT_NONE);
    PACK_SaveLockout(1, true);                            /* lock out 29A */

    SCAN01_SCAN_Rebuild(0xFF);
    expect(SCAN01_SCAN_Count() == 10, "universe: 3 cars + 7 demo stations after the lockout");
    /* venue order: 24, 3, 48 — the capture order is preserved */
    expect(SCAN01_SCAN_GetCurrent()->channel == 0, "universe: starts at car 24");
    expect(SCAN01_SCAN_GetCurrent()->code_type == PACK_CT_CTCSS, "universe: tone type");

    SCAN01_SCAN_CycleGroup();                 /* -> A */
    SCAN01_SCAN_Rebuild(0xFF);
    expect(SCAN01_SCAN_Count() == 9, "group A: cars 24+48 + 7 stations");
    SCAN01_SCAN_CycleGroup();                 /* -> B */
    SCAN01_SCAN_Rebuild(0xFF);
    expect(SCAN01_SCAN_Count() == 7, "group B: stations only");
    SCAN01_SCAN_CycleGroup();                 /* -> C */
    SCAN01_SCAN_Rebuild(0xFF);
    expect(SCAN01_SCAN_Count() == 8, "group C: car 3 + 7 stations");
    SCAN01_SCAN_CycleGroup();                 /* -> FAVS */
    SCAN01_SCAN_Rebuild(0xFF);
    expect(SCAN01_SCAN_Count() == 1, "FAVS: car 24 only (no stations)");
    SCAN01_SCAN_CycleGroup();                 /* -> ALL */
    SCAN01_SCAN_Rebuild(0xFF);
    expect(SCAN01_SCAN_Count() == 10, "ALL: back to 10");
}

static void test_follow_in_universe(void)
{
    boot();
    add_car("24", 450887500u, 0, false, PACK_CT_NONE);
    add_car("48", 451112500u, 1, false, PACK_CT_NONE);   /* group B */
    PACK_SetMyDriver("48");
    SCAN01_SCAN_Rebuild(1);                   /* follow = car index 1 ("48") */
    /* 48 is already in the universe (group ALL) — the follow flag rides it */
    expect(SCAN01_SCAN_Count() == 9, "follow: universe unchanged (2 cars + 7 stations)");
    /* my-driver outside the filter still gets the follow slot */
    SCAN01_SCAN_CycleGroup();                 /* -> A */
    SCAN01_SCAN_Rebuild(1);
    expect(SCAN01_SCAN_Count() == 9, "follow: appended outside the filter (24 + follow 48 + 7 st)");
    {
        uint8_t found = 0;
        for (uint8_t i = 0; i < SCAN01_SCAN_Count(); i++)
            SCAN01_SCAN_JumpTo(i), found |= SCAN01_SCAN_GetCurrent()->follow ? 1u : 0u;
        SCAN01_SCAN_JumpTo(0);
        expect(found, "follow: a follow-flagged slot exists");
    }
}

/* ---- the cycle (S2) ---- */

static void test_walk_dwell(void)
{
    boot();
    add_car("24", 450887500u, 0, false, PACK_CT_NONE);
    add_car("48", 451112500u, 0, false, PACK_CT_NONE);
    add_car("3", 452100000u, 0, false, PACK_CT_NONE);
    SCAN01_SCAN_Rebuild(0xFF);
    SCAN01_SCAN_Start();
    expect_pos(SCAN01_SCAN_GetPosition(), 0, "walk: starts at 0");
    tick(SCAN_DWELL_10MS - 1, false, false);
    expect_pos(SCAN01_SCAN_GetPosition(), 0, "walk: still on 0 before the dwell ends");
    expect(SCAN01_SCAN_Tick10ms(false, false) == SCAN01_SCAN_EV_ENTRY, "walk: EV_ENTRY");
    expect_pos(SCAN01_SCAN_GetPosition(), 1, "walk: advanced to 1");
    tick(SCAN_DWELL_10MS, false, false);
    expect_pos(SCAN01_SCAN_GetPosition(), 2, "walk: advanced to 2");
    /* the universe is 10 entries (3 cars + 7 stations): 8 more advances wrap */
    for (uint8_t i = 3; i < 10; i++)
        tick(SCAN_DWELL_10MS, false, false);
    expect_pos(SCAN01_SCAN_GetPosition(), 9, "walk: at the last entry");
    tick(SCAN_DWELL_10MS, false, false);
    expect_pos(SCAN01_SCAN_GetPosition(), 0, "walk: wraps to 0");
}

static void test_landing(void)
{
    boot();
    add_car("24", 450887500u, 0, false, PACK_CT_CTCSS);
    add_car("48", 451112500u, 0, false, PACK_CT_NONE);
    SCAN01_SCAN_Rebuild(0xFF);
    SCAN01_SCAN_Start();

    /* a candidate opens on entry 0: decode hold, gate passes, lands */
    tick(SCAN_DWELL_10MS - 1, false, false);
    expect(SCAN01_SCAN_Tick10ms(true, true) == SCAN01_SCAN_EV_NONE, "land: candidate detected");
    expect_state(SCAN01_SCAN_GetState(), SCAN01_SCAN_DECODE, "land: decode hold");
    tick(SCAN_DECODE_HOLD_10MS - 1, true, true);
    expect_state(SCAN01_SCAN_GetState(), SCAN01_SCAN_DECODE, "land: still decoding");
    expect(SCAN01_SCAN_Tick10ms(true, true) == SCAN01_SCAN_EV_LANDED, "land: EV_LANDED");
    expect_state(SCAN01_SCAN_GetState(), SCAN01_SCAN_LANDED, "land: landed");
    expect_pos(SCAN01_SCAN_GetPosition(), 0, "land: stays on the entry");

    /* the carrier drops: hang, then advance */
    expect(SCAN01_SCAN_Tick10ms(false, false) == SCAN01_SCAN_EV_HANG, "land: EV_HANG");
    expect_state(SCAN01_SCAN_GetState(), SCAN01_SCAN_HANG, "land: hang");
    tick(SCAN_HANG_10MS - 1, false, false);
    expect_pos(SCAN01_SCAN_GetPosition(), 0, "land: still hanging");
    expect(SCAN01_SCAN_Tick10ms(false, false) == SCAN01_SCAN_EV_ENTRY, "land: resumes");
    expect_pos(SCAN01_SCAN_GetPosition(), 1, "land: advanced after the hang");
}

static void test_tone_gate(void)
{
    boot();
    add_car("24", 450887500u, 0, false, PACK_CT_CTCSS);
    add_car("48", 451112500u, 0, false, PACK_CT_NONE);
    SCAN01_SCAN_Rebuild(0xFF);
    SCAN01_SCAN_Start();

    /* a foreign signal on the tone'd entry: squelch opens, tone never matches */
    tick(SCAN_DWELL_10MS - 1, false, false);
    SCAN01_SCAN_Tick10ms(true, false);
    tick(SCAN_DECODE_HOLD_10MS - 1, true, false);
    expect(SCAN01_SCAN_Tick10ms(true, false) == SCAN01_SCAN_EV_ENTRY, "gate: foreign tone skipped");
    expect_pos(SCAN01_SCAN_GetPosition(), 1, "gate: advanced past the foreign signal");
}

static void test_false_candidate(void)
{
    boot();
    add_car("24", 450887500u, 0, false, PACK_CT_NONE);
    add_car("48", 451112500u, 0, false, PACK_CT_NONE);
    SCAN01_SCAN_Rebuild(0xFF);
    SCAN01_SCAN_Start();

    /* a burst opens the squelch then dies before the decode ends */
    tick(SCAN_DWELL_10MS - 1, false, false);
    SCAN01_SCAN_Tick10ms(true, true);
    SCAN01_SCAN_Tick10ms(false, false);
    expect_state(SCAN01_SCAN_GetState(), SCAN01_SCAN_WALK, "false: back to the walk");
    expect_pos(SCAN01_SCAN_GetPosition(), 1, "false: advanced past the dead burst");
}

static void test_hang_no_chop(void)
{
    boot();
    add_car("24", 450887500u, 0, false, PACK_CT_CTCSS);
    add_car("48", 451112500u, 0, false, PACK_CT_NONE);
    SCAN01_SCAN_Rebuild(0xFF);
    SCAN01_SCAN_Start();
    tick(SCAN_DWELL_10MS - 1, false, false);
    SCAN01_SCAN_Tick10ms(true, true);
    tick(SCAN_DECODE_HOLD_10MS, true, true);    /* landed */
    SCAN01_SCAN_Tick10ms(false, false);         /* drop -> hang */
    expect_state(SCAN01_SCAN_GetState(), SCAN01_SCAN_HANG, "nochop: hanging");
    /* the next syllable within the hang window re-opens — no advance */
    expect(SCAN01_SCAN_Tick10ms(true, true) == SCAN01_SCAN_EV_LANDED, "nochop: re-landed");
    expect_state(SCAN01_SCAN_GetState(), SCAN01_SCAN_LANDED, "nochop: landed again");
    expect_pos(SCAN01_SCAN_GetPosition(), 0, "nochop: still on the entry");
}

/* ---- FOLLOW (S4) ---- */

static void test_follow_interleave(void)
{
    boot();
    for (uint8_t i = 0; i < 10; i++)
        add_car("2", 450000000u + i * 1000u, 0, false, PACK_CT_NONE);
    PACK_SetMyDriver("2");
    SCAN01_SCAN_Rebuild(0);                   /* follow = car 0 ("2") */
    SCAN01_SCAN_Start();

    /* the follow must be revisited every <= 8 walked entries */
    uint8_t last_visit = 0;
    uint8_t steps = 0;
    uint8_t max_gap = 0;
    for (int i = 0; i < 200; i++) {
        if (SCAN01_SCAN_Tick10ms(false, false) == SCAN01_SCAN_EV_ENTRY) {
            steps++;
            if (SCAN01_SCAN_GetCurrent()->follow) {
                uint8_t gap = (uint8_t)(steps - last_visit - 1);  /* entries between */
                if (gap > max_gap)
                    max_gap = gap;
                last_visit = steps;
            }
        }
    }
    expect(max_gap <= SCAN_FOLLOW_INTERLEAVE, "follow: revisit gap <= 8");
    expect(max_gap > 1, "follow: the interleave actually interleaves");
}

/* ---- the CSQ guard (S5) ---- */

static void test_csq_guard(void)
{
    boot();
    add_car("24", 450887500u, 0, false, PACK_CT_NONE);
    add_car("48", 451112500u, 0, false, PACK_CT_NONE);
    SCAN01_SCAN_Rebuild(0xFF);
    SCAN01_SCAN_Start();

    /* an open mic holds entry 0 beyond 5 s */
    tick(SCAN_DWELL_10MS - 1, false, false);
    SCAN01_SCAN_Tick10ms(true, false);        /* candidate */
    tick(SCAN_DECODE_HOLD_10MS, true, false); /* landed (CSQ opens on carrier) */
    tick(SCAN_CSQ_GUARD_10MS - 1, true, false);
    expect(SCAN01_SCAN_Tick10ms(true, false) == SCAN01_SCAN_EV_HANG, "guard: fired");
    tick(1, false, false);                    /* the hang (0 ticks) resolves */
    expect_pos(SCAN01_SCAN_GetPosition(), 1, "guard: moved past the open mic");

    /* walk the full cycle (9 entries: 2 cars + 7 stations) back toward entry 0.
     * Dwell math: the hang-resolution tick consumed one dwell tick, so from
     * pos 1: 7 full dwells advance to pos 8; the 8th dwell's expiry fires the
     * advance TO 0, which the guard redirects to 1 — the walk never rests on
     * the guarded entry. */
    tick(SCAN_DWELL_10MS * 7, false, false);       /* 7 advances: 2..8 */
    expect_pos(SCAN01_SCAN_GetPosition(), 8, "guard: at the last entry's dwell");
    tick(SCAN_DWELL_10MS - 1, false, false);       /* dwell 8 -> 1 */
    expect(SCAN01_SCAN_Tick10ms(false, false) == SCAN01_SCAN_EV_ENTRY, "guard: skip fires");
    expect_pos(SCAN01_SCAN_GetPosition(), 1, "guard: entry 0 skipped for one cycle");
    /* the SECOND visit lands normally: 8 fresh dwells advance 2..8..0 */
    tick(SCAN_DWELL_10MS * 8, false, false);
    expect_pos(SCAN01_SCAN_GetPosition(), 0, "guard: second visit rests on 0");
    tick(SCAN_DWELL_10MS, false, false);
    expect_pos(SCAN01_SCAN_GetPosition(), 1, "guard: and moves on");
}

/* ---- randomized invariants ---- */

static void test_random_walk(void)
{
    boot();
    for (uint8_t i = 0; i < 8; i++)
        add_car("2", 450000000u + i * 1000u, (uint8_t)(i % 3), i == 3, PACK_CT_CTCSS);
    PACK_SetMyDriver("2");
    SCAN01_SCAN_Rebuild(3);
    SCAN01_SCAN_Start();

    unsigned int seed = 12345;
    uint8_t last_pos = SCAN01_SCAN_GetPosition();
    uint16_t last_change = 0;
    uint16_t follow_gap = 0, follow_max = 0;
    bool saw_follow = false;
    for (int i = 0; i < 2000; i++) {
        seed = seed * 1103515245u + 12345u;
        bool sq = (seed >> 16) % 10 < 3;      /* ~30% signal density */
        bool tone = (seed >> 8) % 2 == 0;
        Scan01ScanEvent_t ev = SCAN01_SCAN_Tick10ms(sq, tone);
        if (ev == SCAN01_SCAN_EV_ENTRY) {
            last_change = 0;
            if (SCAN01_SCAN_GetCurrent()->follow) {
                follow_gap = 0;
                saw_follow = true;
            } else if (follow_gap < 255) {
                follow_gap++;
                if (follow_gap > follow_max)
                    follow_max = follow_gap;
            }
            last_pos = SCAN01_SCAN_GetPosition();
        }
        expect(SCAN01_SCAN_GetState() >= SCAN01_SCAN_IDLE &&
               SCAN01_SCAN_GetState() <= SCAN01_SCAN_HANG, "random: state in range");
        if (++last_change > 500) {
            /* the engine can never be stuck: 500 ticks without advancing */
            g_failures++;
            printf("FAIL: random: stuck for 500 ticks\n");
            break;
        }
        (void)last_pos;
    }
    expect(saw_follow, "random: the follow slot was visited");
    expect(follow_max <= SCAN_FOLLOW_INTERLEAVE, "random: follow gap <= 8");
}

int main(void)
{
    test_universe();
    test_follow_in_universe();
    test_walk_dwell();
    test_landing();
    test_tone_gate();
    test_false_candidate();
    test_hang_no_chop();
    test_follow_interleave();
    test_csq_guard();
    test_random_walk();

    printf("scan engine: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
