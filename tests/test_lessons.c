/* Scan 01 — host tests for the fading-legend lesson engine (T6d gate)
 *
 * Compile: gcc -Wall -Werror -Wextra -I. tests/test_lessons.c scan01_lessons.c settings_pack.c pack_bandlock.c -o /tmp/test_lessons
 * Run:    /tmp/test_lessons
 *
 * EEPROM is stubbed with a RAM buffer (same semantics as test_pack.c).
 *
 * Properties under test:
 *   L1 fresh radio: nothing learned, hint appears after 6 s idle
 *   L2 hero first: the PTT lesson is the first hint
 *   L3 rotation: hints cycle through the TEACHABLE lessons only
 *   L4 learning: performing the gesture marks the lesson and persists
 *   L5 persistence: learned bits survive a reload (power cycle)
 *   L6 key activity: any key hides the hint and restarts the idle
 *   L7 can_teach gating: no teaching outside the listening idle
 *   L8 silence: all teachable learned → never a hint again
 *   L9 demo reset: a fresh demo install clears the lesson bits
 *   C1-C4 capability predicates: the demo (no cars / no broadcast / no
 *      driver) teaches 4 lessons; growth adds LOCK and CALL; a broadcast
 *      preset adds FM; unfulfillable gestures are never marked learned
 *   M1-M4 map lifecycle: shown on the first teach-idle, dismissed by any
 *      key, never re-shown this boot, gone when all learned
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "scan01_keys.h"
#include "scan01_lessons.h"
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

static void expect_hint(const char *want, const char *what)
{
    const char *got = SCAN01_LESSONS_CurrentHint();
    g_checks++;
    if (got == NULL || strcmp(got, want) != 0) {
        g_failures++;
        printf("FAIL: %s — got '%s' want '%s'\n", what, got ? got : "(none)", want);
    }
}

/* ---- EEPROM stub (mirrors the base: 8-byte chunks, no-op when unchanged) ---- */

static uint8_t g_eeprom[0x2000];

void EEPROM_ReadBuffer(uint16_t Address, void *pBuffer, uint8_t Size)
{
    memcpy(pBuffer, g_eeprom + Address, Size);
}

void EEPROM_WriteBuffer(uint16_t Address, const void *pBuffer)
{
    if (Address >= 0x2000)
        return;
    if ((Address % 8) != 0) {
        printf("FAIL: unaligned EEPROM write at 0x%04x\n", Address);
        g_failures++;
        return;
    }
    if ((Address % 32) > 24) {
        printf("FAIL: page-wrap write at 0x%04x\n", Address);
        g_failures++;
        return;
    }
    if (memcmp(g_eeprom + Address, pBuffer, 8) != 0)
        memcpy(g_eeprom + Address, pBuffer, 8);
}

static void boot(void)
{
    memset(g_eeprom, 0xFF, sizeof(g_eeprom));
    PACK_Init();                    /* fresh EEPROM → demo install, lessons 0 */
    SCAN01_LESSONS_Init();
}

static void ticks(int n, bool can_teach)
{
    for (int i = 0; i < n; i++)
        SCAN01_LESSONS_Tick10ms(can_teach);
}

static PackCar_t make_car(const char *num, const char *name, const char *team,
                          uint32_t hz, uint8_t tone, uint8_t ctype)
{
    PackCar_t c;
    memset(&c, 0, sizeof(c));
    strncpy(c.number, num, 3);
    strncpy(c.name, name, 10);
    strncpy(c.team, team, 6);
    c.freq_hz = hz;
    c.tone_index = tone;
    c.code_type = ctype;
    c.group = PACK_GROUP_A;
    c.narrow = true;
    return c;
}

/* The demo pack grows: one captured car + a driver unlock the LOCK and
 * CALL lessons (the capability predicates follow the pack). */
static void grow_pack(void)
{
    PackCar_t c = make_car("24", "BYRON W", "HMS", 450887500u, 0, PACK_CT_NONE);
    expect(PACK_AddCapture(&c), "grow: capture a car");
    expect(PACK_SetMyDriver("24"), "grow: set the driver");
}

/* The pack layer has no station API — poke station 6's meta to kind
 * BROADCAST directly in the EEPROM stub (station metas: base 0x1BD0 +
 * 0x40 header + 64*4 car metas = 0x1D10, 10 bytes each; byte 9 low 3 bits
 * = kind, PACK_KIND_BROADCAST = 0). The header CRC does not cover the
 * metas, so the reload stays valid. */
static void make_station_broadcast(void)
{
    uint16_t rec = 0x1D10u + 6 * 10;        /* station 6: the demo's last */
    uint16_t caddr = (rec + 9) & ~7u;       /* the 8-aligned chunk holding meta[9] */
    uint16_t off = (rec + 9) - caddr;
    uint8_t chunk[8];
    EEPROM_ReadBuffer(caddr, chunk, 8);
    chunk[off] &= ~0x07u;                   /* kind = 0 = BROADCAST */
    EEPROM_WriteBuffer(caddr, chunk);
    PACK_Init();                            /* reload the pack with the new kind */
    SCAN01_LESSONS_Init();
}

static void test_fresh_idle_and_hero_first(void)
{
    boot();
    expect(!SCAN01_LESSONS_AllLearned(), "L1: fresh radio has lessons to learn");
    expect(SCAN01_LESSONS_CurrentHint() == NULL, "L1: silent before the idle threshold");
    ticks(599, true);
    expect(SCAN01_LESSONS_CurrentHint() == NULL, "L1: still silent at 5.99 s");
    ticks(1, true);
    expect_hint("PTT = HOLD", "L2: the hero lesson comes first");
}

static void test_rotation_demo(void)
{
    /* the demo teaches only what it can fulfill: PTT, WX, HOME, CATCH */
    boot();
    ticks(600, true);
    expect_hint("PTT = HOLD", "L3: first hint");
    ticks(300, true);
    expect_hint("HOLD 5 = WX", "L3: rotates to weather");
    ticks(300, true);
    expect_hint("HOLD EXIT = HOME", "L3: FM/CALL/LOCK are skipped — the demo cannot fulfill them");
    ticks(300, true);
    expect_hint("HOLD PTT = CATCH", "L3: the demo's hero gesture is taught");
    ticks(300, true);
    expect_hint("PTT = HOLD", "L3: wraps around");
}

static void test_rotation_grown(void)
{
    /* cars + a driver add CALL and LOCK to the rotation */
    boot();
    grow_pack();
    SCAN01_LESSONS_Init();                  /* the capability set changed */
    ticks(600, true);
    expect_hint("PTT = HOLD", "L3g: first hint");
    ticks(300, true);
    expect_hint("HOLD 5 = WX", "L3g: weather");
    ticks(300, true);
    expect_hint("HOLD 9 = CALL", "L3g: my driver");
    ticks(300, true);
    expect_hint("HOLD * = LOCK", "L3g: lockout");
    ticks(300, true);
    expect_hint("HOLD EXIT = HOME", "L3g: home");
    ticks(300, true);
    expect_hint("HOLD PTT = CATCH", "L3g: catch");
    ticks(300, true);
    expect_hint("PTT = HOLD", "L3g: wraps");
}

static void test_learning_and_persistence(void)
{
    boot();
    ticks(600, true);
    expect_hint("PTT = HOLD", "L4: hint before learning");
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_WX);
    expect_hint("PTT = HOLD", "L4: learning a non-shown lesson keeps the hint");
    SCAN01_LESSONS_KeyActivity();
    expect(SCAN01_LESSONS_CurrentHint() == NULL, "L4: the UI hides it");
    expect((PACK_GetLessons() & LESSON_PACK_BIT(LESSON_HOLD5_WX)) != 0,
           "L4: the WX bit is persisted in the pack header");

    /* a power cycle: reload the pack, re-init — the lesson survives */
    PACK_Init();                        /* loads the demo pack from EEPROM */
    SCAN01_LESSONS_Init();
    expect((PACK_GetLessons() & LESSON_PACK_BIT(LESSON_HOLD5_WX)) != 0,
           "L5: learned bits survive a reload");
    expect(!SCAN01_LESSONS_AllLearned(), "L5: the rest is still unlearned");
    ticks(600, true);
    expect_hint("PTT = HOLD", "L5: teaching resumes where it left off");
}

static void test_key_activity_and_gating(void)
{
    boot();
    ticks(600, true);
    expect_hint("PTT = HOLD", "L6: hint active");
    SCAN01_LESSONS_KeyActivity();
    expect(SCAN01_LESSONS_CurrentHint() == NULL, "L6: any key hides the hint");
    ticks(599, true);
    expect(SCAN01_LESSONS_CurrentHint() == NULL, "L6: idle restarts from zero");
    ticks(1, true);
    expect_hint("PTT = HOLD", "L6: hint returns after a full idle");

    /* not teachable (HOLD, typing, locked): no hints even after minutes */
    ticks(1000, false);
    expect(SCAN01_LESSONS_CurrentHint() == NULL, "L7: no teaching when not can_teach");
    ticks(1000, false);
    expect(SCAN01_LESSONS_CurrentHint() == NULL, "L7: still silent");
    ticks(600, true);
    expect_hint("PTT = HOLD", "L7: teaching resumes when can_teach returns");
}

static void test_silence_and_reset(void)
{
    boot();
    /* learn every TEACHABLE lesson of the demo through its actions */
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_HOLD);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_WX);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_HOME);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_CAPTURE);
    expect(SCAN01_LESSONS_AllLearned(), "L8: all teachable lessons learned");
    uint8_t demo_all = LESSON_PACK_BIT(LESSON_PTT_HOLD)
                     | LESSON_PACK_BIT(LESSON_HOLD5_WX)
                     | LESSON_PACK_BIT(LESSON_HOLDEXIT_HOME)
                     | LESSON_PACK_BIT(LESSON_HOLDPTT_CATCH);
    expect(PACK_GetLessons() == demo_all, "L8: the header holds exactly the teachable bits");
    ticks(10000, true);
    expect(SCAN01_LESSONS_CurrentHint() == NULL, "L8: a learned radio never hints");
    /* RESUME also learns the PTT lesson (idempotent) */
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_RESUME);
    expect(SCAN01_LESSONS_AllLearned(), "L8: RESUME is the same lesson");
    /* gestures the demo cannot fulfill are not lessons: no bits, no lie */
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_BRD);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_MYDRIVER);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_LOCKOUT);
    expect(PACK_GetLessons() == demo_all, "L8: unfulfillable gestures mark nothing");

    /* a wiped EEPROM reinstalls the demo with a clean slate */
    boot();
    expect(PACK_GetLessons() == 0, "L9: demo install clears the lesson bits");
    expect(!SCAN01_LESSONS_AllLearned(), "L9: a fresh radio teaches again");
}

static void test_capabilities(void)
{
    boot();
    expect(SCAN01_LESSONS_MapCount() == 4, "C1: the demo teaches four lessons");
    expect(strcmp(SCAN01_LESSONS_MapText(0), "PTT = HOLD") == 0, "C1: hero first");
    expect(strcmp(SCAN01_LESSONS_MapText(3), "HOLD PTT = CATCH") == 0, "C1: catch last");

    /* growing the pack (a captured car + a driver) adds LOCK and CALL */
    grow_pack();
    expect(SCAN01_LESSONS_MapCount() == 6, "C2: cars + driver add two lessons");
    bool has_lock = false, has_fm = false;
    for (uint8_t i = 0; i < SCAN01_LESSONS_MapCount(); i++) {
        const char *t = SCAN01_LESSONS_MapText(i);
        if (strcmp(t, "HOLD * = LOCK") == 0)
            has_lock = true;
        if (strcmp(t, "HOLD 0 = FM") == 0)
            has_fm = true;
    }
    expect(has_lock, "C2: lockout appears with cars");
    expect(!has_fm, "C2: FM stays absent without broadcast presets");

    /* learning is gated: the FM gesture on this pack marks nothing */
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_BRD);
    expect((PACK_GetLessons() & LESSON_PACK_BIT(LESSON_HOLD0_FM)) == 0,
           "C3: an unfulfillable gesture is not a lesson");

    /* a broadcast preset (station meta poked in the stub) adds FM */
    make_station_broadcast();
    expect(SCAN01_LESSONS_MapCount() == 7, "C4: broadcast presets add FM");
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_BRD);
    expect((PACK_GetLessons() & LESSON_PACK_BIT(LESSON_HOLD0_FM)) != 0,
           "C4: FM is now a real lesson");
    expect(!SCAN01_LESSONS_AllLearned(), "C4: the rest is still unlearned");
    /* learn everything teachable → the legend fades */
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_HOLD);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_WX);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_MYDRIVER);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_LOCKOUT);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_HOME);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_CAPTURE);
    expect(SCAN01_LESSONS_AllLearned(), "C4: all seven learned");
    ticks(10000, true);
    expect(SCAN01_LESSONS_CurrentHint() == NULL, "C4: and silence");
}

static void test_map_lifecycle(void)
{
    boot();
    expect(!SCAN01_LESSONS_MapActive(), "M1: no map before the idle threshold");
    ticks(600, true);
    expect(SCAN01_LESSONS_MapActive(), "M1: the first teach-idle shows the map");
    expect(SCAN01_LESSONS_CurrentHint() != NULL, "M1: the nudge is armed behind it");
    expect(SCAN01_LESSONS_MapCount() == 4, "M1: the demo map has four rows");

    /* any key dismisses the map for the rest of the boot */
    SCAN01_LESSONS_KeyActivity();
    expect(!SCAN01_LESSONS_MapActive(), "M2: a key dismisses the map");
    ticks(1000, true);
    expect(!SCAN01_LESSONS_MapActive(), "M2: the map never returns this boot");
    expect(SCAN01_LESSONS_CurrentHint() != NULL, "M2: the nudges keep teaching");

    /* a new boot earns one more map */
    SCAN01_LESSONS_Init();
    expect(!SCAN01_LESSONS_MapActive(), "M3: fresh boot: armed but not shown yet");
    ticks(600, true);
    expect(SCAN01_LESSONS_MapActive(), "M3: the next boot shows the map again");

    /* all learned: no map, no nudges */
    SCAN01_LESSONS_KeyActivity();
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_HOLD);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_WX);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_HOME);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_CAPTURE);
    expect(SCAN01_LESSONS_AllLearned(), "M4: all learned");
    ticks(10000, true);
    expect(!SCAN01_LESSONS_MapActive(), "M4: a learned radio shows no map");
    expect(SCAN01_LESSONS_CurrentHint() == NULL, "M4: and no nudges");
}

int main(void)
{
    test_fresh_idle_and_hero_first();
    test_rotation_demo();
    test_rotation_grown();
    test_learning_and_persistence();
    test_key_activity_and_gating();
    test_silence_and_reset();
    test_capabilities();
    test_map_lifecycle();

    printf("lessons: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
