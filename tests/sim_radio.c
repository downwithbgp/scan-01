/* Scan 01 — the headless radio (the "screenshot without hardware")
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
 * Drives the REAL Scan 01 UI (scan01_ui.c + key layer + pack + fonts +
 * ui/helper.c) through scripted key sequences against the stubbed radio,
 * then: (1) asserts the pixel budgets that the (hw) screenshot gate was
 * going to check, (2) dumps every screen as P4 PBM into ./screenshots/
 * and prints an ASCII preview. CI turns the PBMs into PNG artifacts.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/fm.h"
#include "dcs.h"
#include "driver/bk4819.h"
#include "driver/st7565.h"
#include "font_racing.h"
#include "scan01_edit.h"
#include "scan01_keys.h"
#include "scan01_lessons.h"
#include "scan01_scan.h"
#include "misc.h"
#include "radio.h"
#include "scan01_keys.h"
#include "scan01_ui.h"
#include "settings.h"
#include "settings_pack.h"

extern uint8_t g_sim_screen[8][128];
void SIM_EEPROM_Reset(void);
void SIM_EEPROM_Poke(uint16_t address, const void *data, uint16_t size);
void SIM_EEPROM_SetReadOnly(bool readonly);
void SIM_SetCssResult(BK4819_CssScanResult_t result, uint32_t cdcss, uint16_t ctcss);
void SIM_SetSignal(bool squelch_open, bool tone_ok);

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

static void key(KEY_Code_t k) { SCAN01_UI_ProcessKeys(k, true, false); }
static void release(KEY_Code_t k) { SCAN01_UI_ProcessKeys(k, false, false); }
static void release_held(KEY_Code_t k) { SCAN01_UI_ProcessKeys(k, false, true); }
static void held(KEY_Code_t k) { SCAN01_UI_ProcessKeys(k, true, true); }

static void ticks(int n)
{
    for (int i = 0; i < n; i++)
        SCAN01_UI_Tick10ms();
}

static void ptt_tap(void) { key(KEY_PTT); release(KEY_PTT); }
static void ptt_hold(void) { key(KEY_PTT); ticks(80); release(KEY_PTT); }

static void digit(char d) { key(KEY_0 + (d - '0')); release(KEY_0 + (d - '0')); }

static void render(void) { UI_DisplayScan01(); }

/* ---- assertions on the captured screen ---- */

static int line_has_ink(int row, int x0, int x1)   /* row 0..7, pixel columns */
{
    for (int x = x0; x <= x1; x++)
        if (g_sim_screen[row][x] != 0)
            return 1;
    return 0;
}

static void assert_row_empty(int strip, const char *what)
{
    expect(!line_has_ink(strip, 0, 127), what);   /* strip 7 = LCD rows 56-63 */
}

/* the 32 px number zone is right-aligned: for a width W the ink on the
 * number's top strip starts at >= 127-W. top_row = the FIRST LCD pixel row
 * of the zone (render line N = LCD rows 8*(N+1)..8*(N+1)+7). */
static void assert_right_aligned(const char *text, int top_row, const char *what)
{
    int w = RACING_TextWidth(text);
    int left = 127 - w;
    int strip = top_row / 8;
    /* nothing left of the number's start on its TOP strip (line 0 of the
     * zone holds only the number — the name/team live on the lower strips) */
    expect(!line_has_ink(strip, 0, left - 1), what);
    expect(line_has_ink(strip, left, 127), what);
}

/* ---- the demo+ pack: 8 cars (2 at venue 1) on top of the demo stations ---- */

static void pack_car(const char *num, const char *name, const char *team,
                     uint8_t venue, bool favorite)
{
    PackCar_t c;
    memset(&c, 0, sizeof(c));
    strncpy(c.number, num, 3);
    strncpy(c.name, name, 10);
    strncpy(c.team, team, 6);
    c.freq_hz = 450000000u + (uint32_t)(num[0] * 1000u);
    c.narrow = true;
    c.venue = venue;
    c.favorite = favorite;
    if (!PACK_AddCapture(&c))
        printf("sim: pack_car %s failed\n", num);
}

static void boot_radio(void)
{
    SIM_EEPROM_Reset();
    PACK_Init();
    SCAN01_UI_Init();
    gEEPROM_RSSI_CALIB[0][0] = 100;         /* make the RSSI bar show */
    gEEPROM_RSSI_CALIB[0][3] = 500;
    gEeprom.VOLUME_GAIN = 12;
    gEeprom.VOLUME_GAIN_BACKUP = 12;
}

/* ---- PBM dump + ASCII preview ---- */

static void dump(const char *name)
{
    char path[64];
    FILE *f;

    system("mkdir -p screenshots");

    snprintf(path, sizeof(path), "screenshots/%s.pbm", name);
    f = fopen(path, "wb");
    if (f == NULL) {
        printf("sim: cannot write %s\n", path);
        return;
    }
    fprintf(f, "P4\n128 64\n");
    for (int row = 0; row < 64; row++) {
        uint8_t bytes[16] = { 0 };          /* PBM rows must start clean */
        for (int x = 0; x < 128; x++) {
            uint8_t bit = (g_sim_screen[row / 8][x] >> (row % 8)) & 1;
            if (bit)
                bytes[x / 8] |= (uint8_t)(0x80 >> (x % 8));
        }
        fwrite(bytes, 1, 16, f);
    }
    fclose(f);
}

static void preview(const char *name)
{
    printf("=== %s ===\n", name);
    for (int row = 0; row < 64; row++) {
        for (int x = 0; x < 128; x++)
            putchar((g_sim_screen[row / 8][x] >> (row % 8)) & 1 ? '#' : '.');
        putchar('\n');
    }
}

static void shot(const char *name, bool show_preview)
{
    render();
    dump(name);
    if (show_preview)
        preview(name);
}

/* ---- scenarios ---- */

int main(void)
{
    char num[8];

    /* 1. boot identity */
    boot_radio();
    shot("01-identity", true);
    assert_row_empty(7, "identity: last line empty");
    expect(line_has_ink(2, 30, 100) || line_has_ink(3, 30, 100),
           "identity: the pack line renders");

    /* 2. SCAN on a car (identity expires → the tick starts the scan) */
    pack_car("24", "BYRON W", "HMS", 0, true);
    pack_car("29A", "HAMILTON L", "MCL", 1, false);
    pack_car("8", "KYLE B", "TRD", 0, true);
    pack_car("100", "ANDRETTI M", "CGR", 0, false);
    pack_car("3", "DILLON A", "RCR", 1, false);
    pack_car("48", "JOHNSON J", "HMS", 0, false);
    pack_car("19", "TRUEX M", "JGR", 0, false);
    pack_car("12", "BLANEY R", "PEN", 0, false);
    PACK_SetMyDriver("24");
    SCAN01_UI_Init();                       /* re-init after the pack grew */
    ticks(80);                              /* identity expires */
    ticks(1);                               /* the scan self-heals */
    shot("02-scan-car", true);
    strncpy(num, "24", sizeof(num));
    assert_right_aligned("24", 8, "scan: number zone right-aligned");

    /* 3. SCAN on a station (poke the current channel — a screenshot tool) */
    gRxVfo->CHANNEL_SAVE = 8;               /* first station channel */
    shot("03-scan-station", true);

    /* 4. HOLD */
    gRxVfo->CHANNEL_SAVE = 0;
    ptt_tap();
    shot("04-hold", true);
    assert_row_empty(7, "hold: last line empty");

    /* 5. LIST with the selection on car 0 (* = the printed SCAN label) */
    key(KEY_STAR);
    release(KEY_STAR);                      /* * short → SCAN */
    key(KEY_UP);
    release(KEY_UP);                        /* UP short → LIST */
    shot("05-list", true);
    {
        int inv = 0;
        for (int x = 0; x < 128; x++)
            if (g_sim_screen[1][x] == 0xFF)     /* line 0: only the number punches out */
                inv++;
        expect(inv > 100, "list: selected row is inverted");
    }
    {
        int clean = 1;
        for (int row = 4; row <= 5; row++)      /* lines 3-4: text + rules, no blocks */
            for (int x = 0; x < 128; x++)
                if (g_sim_screen[row][x] == 0xFF)
                    clean = 0;
        expect(clean, "list: unselected rows are not inverted");
    }

    /* 6. lockout on car 0, then move to car 1: the window [0,1,2] shows
     * the locked car's strike (block 0) and the venue rule (block 1) */
    held(KEY_STAR);
    key(KEY_DOWN);
    shot("06-list-lockout-divider", true);
    {
        int strike = 0, rule = 0;
        for (int x = 20; x < 127; x++) {
            if (g_sim_screen[2][x] & 0x01)     /* strike: content y=8 of block 0 */
                strike = 1;
            if (g_sim_screen[3][x] & 0x01)     /* rule: content y=16 of block 1 */
                rule = 1;
        }
        expect(strike, "list: locked car struck through");
        expect(rule, "list: venue divider rule visible");
    }

    /* 7. CAPTURE empty — with a live tone decode (94.8 Hz) */
    SIM_SetCssResult(BK4819_CSS_RESULT_CTCSS, 0, 948);
    key(KEY_STAR);                          /* back to SCAN */
    ptt_hold();
    shot("07-capture-empty", true);

    /* 8. CAPTURE typed + saved with the decoded tone:
     * "24" duplicates the pack car -> an ALT entry inheriting its name */
    digit('2');
    digit('4');
    shot("08-capture-24", true);
    /* the top strip carries the tone caption now — check the bottom strip */
    assert_right_aligned("24", 48, "capture: number input right-aligned");
    ptt_tap();                                  /* SAVE */
    expect(PACK_CarCount() == 9, "save: entry appended");
    {
        const PackCar_t *c = PACK_GetCar(8);
        expect(c != NULL && strcmp(c->number, "24") == 0, "save: number");
        expect(c != NULL && strcmp(c->name, "ALT BYRON") == 0, "save: ALT inherits the name");
        expect(c != NULL && c->origin == PACK_ORIGIN_CAPTURED, "save: origin captured");
        expect(c != NULL && !c->verified, "save: unverified");
        expect(c != NULL && c->code_type == PACK_CT_CTCSS, "save: tone type");
        expect(c != NULL && c->tone_index == DCS_GetCtcssCode(948), "save: tone index");
    }

    /* 9. the tone shows on the CAPTURE line; a fresh number saves as NEW */
    ptt_hold();
    shot("09-capture-tone", true);
    expect(line_has_ink(3, 0, 45), "capture: the tone line renders");
    digit('7');
    digit('7');
    ptt_tap();
    expect(PACK_CarCount() == 10, "save: second entry");
    {
        const PackCar_t *c = PACK_GetCar(9);
        expect(c != NULL && strcmp(c->number, "77") == 0, "save: fresh number");
        expect(c != NULL && strcmp(c->name, "NEW") == 0, "save: default name NEW");
    }
    SIM_SetCssResult(BK4819_CSS_RESULT_NOT_FOUND, 0, 0);

    /* 9b. the scan engine: the walk lands on a tone'd signal. The earlier
     * lockout scenario excluded car 24, so the walk is 29A, 8, 100, 3, 48 —
     * after 4 dwells the signal opens on car 48 (ch 5). */
    SCAN01_SCAN_JumpTo(0);                  /* deterministic walk start */
    SIM_SetSignal(false, true);             /* a quiet band */
    ticks(8 * 4);                           /* four dwells: pos 4 = car "48" */
    SIM_SetSignal(true, true);              /* the car keys up with its tone */
    ticks(SCAN_DECODE_HOLD_10MS + 1);       /* the decode hold (the first tick
                                             * only transitions to DECODE) */
    expect(SCAN01_SCAN_GetState() == SCAN01_SCAN_LANDED, "engine: landed on the signal");
    shot("09b-scan-landed", true);
    expect(gRxVfo->CHANNEL_SAVE == 5, "engine: tuned the signalling car (ch 5 = car 48)");
    SIM_SetSignal(false, true);             /* unkeyed */
    ticks(SCAN_HANG_10MS + 1);              /* the transition tick + the 25-tick hang */
    expect(SCAN01_SCAN_GetState() == SCAN01_SCAN_WALK, "engine: resumed after the hang");

    /* 10. BRD */
    held(KEY_0);
    shot("10-brd", true);
    expect(gFmRadioMode, "brd: FM mode active");

    /* 10. WX (from BRD — the FM teardown must have run) */
    held(KEY_5);
    shot("11-wx", true);
    expect(!gFmRadioMode, "wx from brd: FM torn down");
    expect(line_has_ink(7, 0, 127), "wx: hint line");
    key(KEY_DOWN);
    shot("12-wx-2", true);

    /* 11. SETUP pages */
    key(KEY_STAR);
    key(KEY_MENU);
    release(KEY_MENU);                      /* M short → SETUP */
    shot("13-setup-pack", true);
    key(KEY_MENU);
    shot("14-setup-audio", true);
    key(KEY_MENU);
    shot("15-setup-display", true);
    key(KEY_MENU);
    shot("16-setup-info", true);
    key(KEY_EXIT);                          /* leave SETUP */

    /* 12. the name editor: LIST → M on car 0 → type BYRON */
    ticks(80);
    key(KEY_UP);
    release(KEY_UP);                        /* UP short → LIST */
    key(KEY_MENU);
    release(KEY_MENU);                      /* M short → SETUP → the editor */
    shot("17-edit-empty", true);
    digit('2'); digit('2');                 /* B */
    digit('9'); digit('9'); digit('9');     /* Y */
    digit('7'); digit('7'); digit('7');     /* R */
    digit('6'); digit('6'); digit('6');     /* O */
    ticks(160);                             /* the multi-tap window expires */
    digit('6'); digit('6');                 /* N */
    shot("18-edit-byron", true);
    expect(strcmp(SCAN01_EDIT_GetBuffer(), "BYRON") == 0, "edit: multi-tap types BYRON");

    /* 12b. key lock: PTT saves → LIST, `*` short → SCAN, then the real
     * press→held→release M chain locks; the shot must show the persistent
     * state line (tick past the 2 s flash), and the release after the hold
     * must not re-toggle the lock */
    key(KEY_PTT);                           /* save BYRON → LIST */
    key(KEY_STAR);
    release(KEY_STAR);                      /* * short → SCAN */
    ticks(200);                             /* any flash expires before the negative check */
    render();                               /* the frame must be current, not the editor's */
    expect(!line_has_ink(6, 40, 90), "keylock: unlocked SCAN has no lock text");
    key(KEY_MENU);                          /* press... */
    held(KEY_MENU);                         /* ...hold → KEYLOCK */
    release_held(KEY_MENU);                 /* release after the hold: consumed */
    expect(gEeprom.KEY_LOCK, "keylock: M held sets KEY_LOCK");
    ticks(200);                             /* the KEYLOCK flash expires */
    shot("19-keylock", true);
    /* strip 6 = framebuffer row 5 = the state line; "KEYLOCK" spans
     * x≈9–58, while the unlocked "SCAN" stops at x≈37 — so ink in 40–90
     * proves the persistent lock branch rendered */
    expect(line_has_ink(6, 40, 90), "keylock: the persistent state line reads KEYLOCK");
    key(KEY_MENU);                          /* press... */
    held(KEY_MENU);                         /* ...hold → unlock */
    release_held(KEY_MENU);
    expect(!gEeprom.KEY_LOCK, "keylock: M held again clears KEY_LOCK, release keeps it off");

    /* 12c. the fading legend: fresh slate. The FIRST teach-idle shows the
     * full-screen map (the whole legend at once); any key dismisses it and
     * the one-line nudges take over; each gesture learns its lesson; all
     * six learned → the radio goes permanently quiet */
    PACK_SetLessons(0);                     /* fresh radio: everything to learn */
    SCAN01_LESSONS_Init();
    ticks(600);                             /* 6 s idle */
    render();
    expect(SCAN01_LESSONS_MapActive(), "map: the first teach-idle shows the map");
    expect(strcmp(SCAN01_LESSONS_CurrentHint(), "PTT = HOLD") == 0,
           "map: the hero nudge is armed behind it");
    expect(line_has_ink(1, 0, 127), "map: row 0 renders the hero lesson");
    expect(line_has_ink(7, 0, 127), "map: row 6 renders the footer hint");
    expect(!line_has_ink(6, 120, 127), "map: the HOME row is not clipped at the right edge");
    shot("21-map", true);

    key(KEY_STAR);                          /* any key dismisses the map */
    expect(!SCAN01_LESSONS_MapActive(), "map: a key dismisses it");
    ticks(600);
    render();
    expect(!SCAN01_LESSONS_MapActive(), "map: never returns this boot");
    expect(strcmp(SCAN01_LESSONS_CurrentHint(), "PTT = HOLD") == 0,
           "lessons: the hero lesson is the first nudge");
    expect(line_has_ink(6, 40, 90), "lessons: the nudge renders in the state line");
    shot("22-hint", true);

    ptt_tap();                              /* PTT short → HOLD: lesson one */
    key(KEY_STAR);
    release(KEY_STAR);                      /* * short → SCAN (deferred from HOLD) */
    ticks(600);
    render();
    expect(strcmp(SCAN01_LESSONS_CurrentHint(), "HOLD 5 = WX") == 0,
           "lessons: the next hint is weather");
    held(KEY_5);                            /* hold 5 → WX: lesson two */
    expect((PACK_GetLessons() & LESSON_PACK_BIT(LESSON_HOLD5_WX)) != 0,
           "lessons: WX is learned and persisted");
    key(KEY_STAR);                          /* leave WX (immediate) */
    ticks(600);
    render();
    expect(strcmp(SCAN01_LESSONS_CurrentHint(), "HOLD 0 = FM") == 0,
           "lessons: next is FM");
    held(KEY_0);                            /* hold 0 → BRD: lesson three */
    key(KEY_STAR);                          /* leave BRD (immediate) */
    ticks(600);
    render();
    expect(strcmp(SCAN01_LESSONS_CurrentHint(), "HOLD 9 = CALL") == 0,
           "lessons: next is my driver");
    held(KEY_9);                            /* hold 9 → the driver (24): lesson four */
    key(KEY_STAR);
    release(KEY_STAR);                      /* * short → SCAN (deferred from HOLD) */
    ticks(600);
    render();
    expect(strcmp(SCAN01_LESSONS_CurrentHint(), "HOLD * = LOCK") == 0,
           "lessons: next is lockout");
    ptt_tap();                              /* HOLD the car */
    held(KEY_STAR);                         /* hold * → lockout: lesson five */
    key(KEY_STAR);
    release(KEY_STAR);                      /* * short → SCAN (deferred from HOLD) */
    ticks(600);
    render();
    expect(strcmp(SCAN01_LESSONS_CurrentHint(), "HOLD EXIT = HOME") == 0,
           "lessons: the last lesson is home");
    held(KEY_EXIT);                         /* long-EXIT → HOME: lesson six */
    key(KEY_STAR);
    release(KEY_STAR);                      /* * short → SCAN (deferred from LIST) */
    expect(SCAN01_LESSONS_AllLearned(), "lessons: all six learned");
    expect(PACK_GetLessons() == LESSON_PACK_ALL, "lessons: the header holds all bits");
    ticks(600);
    render();
    expect(SCAN01_LESSONS_CurrentHint() == NULL, "lessons: the legend has faded");
    expect(!SCAN01_LESSONS_MapActive(), "lessons: no map either");
    expect(!line_has_ink(6, 40, 90), "lessons: the state line is quiet again");
    shot("23-quiet", true);

    /* 13. NO PACK boot (a failing EEPROM: even the demo install fails) */
    SIM_EEPROM_Reset();
    SIM_EEPROM_SetReadOnly(true);
    PACK_Init();
    SCAN01_UI_Init();
    shot("20-nopack", true);
    expect(!PACK_IsValid(), "nopack: no valid pack");
    expect(line_has_ink(2, 30, 100) || line_has_ink(3, 30, 100),
           "nopack: the NO PACK line renders");

    printf("sim: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
