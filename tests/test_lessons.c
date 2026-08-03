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
 *   L3 rotation: hints cycle every 3 s through the unlearned lessons
 *   L4 learning: performing the gesture marks the lesson and persists
 *   L5 persistence: learned bits survive a reload (power cycle)
 *   L6 key activity: any key hides the hint and restarts the idle
 *   L7 can_teach gating: no teaching outside the listening idle
 *   L8 silence: all learned → never a hint again
 *   L9 demo reset: a fresh demo install clears the lesson bits
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

static void test_rotation(void)
{
    boot();
    ticks(600, true);
    expect_hint("PTT = HOLD", "L3: first hint");
    ticks(300, true);
    expect_hint("HOLD 5 = WX", "L3: rotates to weather");
    ticks(300, true);
    expect_hint("HOLD 0 = FM", "L3: rotates to FM");
    ticks(300, true);
    expect_hint("HOLD 9 = CALL", "L3: rotates to my driver");
    ticks(300, true);
    expect_hint("HOLD * = LOCK", "L3: rotates to lockout");
    ticks(300, true);
    expect_hint("HOLD EXIT = HOME", "L3: rotates to home");
    ticks(300, true);
    expect_hint("PTT = HOLD", "L3: wraps around");
}

static void test_learning_and_persistence(void)
{
    boot();
    /* learn WX by performing the gesture; the shown hint advances at once */
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
    /* learn all six through their actions (RESUME counts as PTT too) */
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_HOLD);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_WX);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_BRD);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_MYDRIVER);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_LOCKOUT);
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_HOME);
    expect(SCAN01_LESSONS_AllLearned(), "L8: all six learned");
    expect(PACK_GetLessons() == LESSON_PACK_ALL, "L8: the header holds all bits");
    ticks(10000, true);
    expect(SCAN01_LESSONS_CurrentHint() == NULL, "L8: a learned radio never hints");
    /* RESUME also learns the PTT lesson (idempotent) */
    SCAN01_LESSONS_MarkLearned(SCAN01_ACT_RESUME);
    expect(SCAN01_LESSONS_AllLearned(), "L8: RESUME is the same lesson");

    /* a wiped EEPROM reinstalls the demo with a clean slate */
    boot();
    expect(PACK_GetLessons() == 0, "L9: demo install clears the lesson bits");
    expect(!SCAN01_LESSONS_AllLearned(), "L9: a fresh radio teaches again");
}

int main(void)
{
    test_fresh_idle_and_hero_first();
    test_rotation();
    test_learning_and_persistence();
    test_key_activity_and_gating();
    test_silence_and_reset();

    printf("lessons: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
