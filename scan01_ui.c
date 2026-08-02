/* Scan 01 — the display implementation (task T6, vision §5)
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

#include "app/chFrScanner.h"
#include "driver/bk4819.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "font_racing.h"
#include "frequencies.h"
#include "misc.h"
#include "pack_bandlock.h"
#include "radio.h"
#include "scan01_keys.h"
#include "settings.h"
#include "settings_pack.h"
#include "ui/helper.h"
#include "ui/status.h"

/* ---- internal states (a subset of the key layer's; BRD/WX/SETUP = T6b) ---- */
typedef enum {
    S1_ST_SCAN = 0,
    S1_ST_HOLD,
    S1_ST_LIST,
    S1_ST_CAPTURE,
} S1State_t;

static S1State_t g_st = S1_ST_SCAN;
static int16_t   g_list_sel;          /* 0..PACK_CarCount(); == count = ＋ NEW row */
static uint32_t  g_capture_freq;      /* the frequency the CAPTURE form will save */
static char      g_flash[15];         /* transient state-line message */
static uint16_t  g_flash_10ms;
static bool      g_identity;          /* boot identity / NO PACK screen (vision §5.8) */
static uint16_t  g_identity_10ms;

/* ---- 8 px state glyphs (◉ ◼ ▸) — hand-drawn, column-major ---- */
static const uint8_t GLYPH_SCAN[8] = { 0x3C, 0x42, 0x99, 0xA5, 0xA5, 0x99, 0x42, 0x3C };
static const uint8_t GLYPH_HOLD[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static const char *const KIND_TEXT[8] = {
    "BROADCAST", "CONTROL", "PA", "OFFICIALS", "SAFETY", "RADIO", "MEDIA", "OTHER"
};

/* ---- small helpers ---- */

static void Flash(const char *msg)
{
    strncpy(g_flash, msg, sizeof(g_flash) - 1);
    g_flash[sizeof(g_flash) - 1] = 0;
    g_flash_10ms = 200;                     /* 2 s */
}

static void SetState(S1State_t st)
{
    g_st = st;
    switch (st) {
    case S1_ST_SCAN:    SCAN01_KEYS_SetUiState(SCAN01_UI_SCAN); break;
    case S1_ST_HOLD:    SCAN01_KEYS_SetUiState(SCAN01_UI_HOLD); break;
    case S1_ST_LIST:    SCAN01_KEYS_SetUiState(SCAN01_UI_LIST); break;
    case S1_ST_CAPTURE: SCAN01_KEYS_SetUiState(SCAN01_UI_CAPTURE);
                        SCAN01_TYPE_SetAutoCommit(false); break;  /* §4.5: no timeout in the form */
    }
    gUpdateDisplay = true;
}

/* "450887500" → "450.8875"; trailing zeros trimmed ("162.55", "88.7") */
static void FormatFreq(char *out, uint32_t hz)
{
    uint32_t mhz = hz / 1000000u;
    uint32_t frac = hz % 1000000u;
    char buf[8];

    if (frac == 0) {
        sprintf(out, "%u", (unsigned int)mhz);
        return;
    }
    sprintf(buf, "%06u", (unsigned int)frac);
    int len = 6;
    while (len > 1 && buf[len - 1] == '0')
        len--;
    buf[len] = 0;
    sprintf(out, "%u.%s", (unsigned int)mhz, buf);
}

/* "451.1125" → 451112500. The key layer only commits freq strings with a
 * point, and int ≤ 470 (3 digits) — no overflow possible. */
static bool ParseFreq(const char *s, uint32_t *hz)
{
    uint32_t mhz = 0, frac = 0;
    int frac_len = 0;
    bool point = false;

    for (const char *p = s; *p; p++) {
        if (*p == '.') {
            if (point)
                return false;
            point = true;
            continue;
        }
        if (*p < '0' || *p > '9')
            return false;
        if (!point) {
            mhz = mhz * 10u + (uint32_t)(*p - '0');
        } else {
            if (frac_len >= 5)
                return false;
            frac = frac * 10u + (uint32_t)(*p - '0');
            frac_len++;
        }
    }
    if (!point)
        return false;
    while (frac_len < 6) {
        frac *= 10u;
        frac_len++;
    }
    *hz = mhz * 1000000u + frac;
    return true;
}

static const PackCar_t *CarByChannel(uint16_t channel)
{
    for (uint8_t i = 0; i < PACK_CarCount(); i++)
        if (PACK_GetCar(i)->channel == channel)
            return PACK_GetCar(i);
    return NULL;
}

static const PackStation_t *StationByChannel(uint16_t channel)
{
    for (uint8_t i = 0; i < PACK_StationCount(); i++)
        if (PACK_GetStation(i)->channel == channel)
            return PACK_GetStation(i);
    return NULL;
}

static const PackCar_t *CarByNumber(const char *number)
{
    for (uint8_t i = 0; i < PACK_CarCount(); i++)
        if (strcmp(PACK_GetCar(i)->number, number) == 0)
            return PACK_GetCar(i);
    return NULL;
}

static bool CarLocked(const PackCar_t *car)
{
    const uint8_t *bitmap = PACK_LockoutBitmap();
    uint8_t i = 0;

    while (i < PACK_CarCount()) {
        if (PACK_GetCar(i) == car)
            break;
        i++;
    }
    if (i >= PACK_CarCount())
        return false;
    return (bitmap[i >> 3] & (1u << (i & 7))) != 0;
}

/* ---- radio actions ---- */

static void StartScan(void)
{
    if (!PACK_IsValid()) {
        Flash("NO PACK");
        return;
    }
    CHFRSCANNER_Start(true, SCAN_FWD);      /* true: Stop() restores the pre-scan channel */
    SetState(S1_ST_SCAN);
}

static void TuneChannel(uint16_t channel)
{
    gEeprom.MrChannel[gEeprom.RX_VFO] = channel;
    gEeprom.ScreenChannel[gEeprom.RX_VFO] = channel;
    gRxVfo->CHANNEL_SAVE = channel;
    RADIO_ConfigureChannel(gEeprom.RX_VFO, VFO_CONFIGURE_RELOAD);
    RADIO_SetupRegisters(true);
    gUpdateDisplay = true;
}

static void TuneFreq(uint32_t hz)
{
    gRxVfo->freq_config_RX.Frequency = hz;
    gRxVfo->Modulation = (hz >= 108000000u && hz < 137000000u) ? MODULATION_AM : MODULATION_FM;
    gRxVfo->Band = FREQUENCY_GetBand(hz);   /* RSSI calibration + band limits */
    RADIO_ApplyOffset(gRxVfo);
    RADIO_ConfigureSquelchAndOutputPower(gRxVfo);
    RADIO_SetupRegisters(true);
    gUpdateDisplay = true;
}

static void TuneCar(const PackCar_t *car)
{
    TuneChannel(car->channel);
    SetState(S1_ST_HOLD);
}

/* ---- LIST selection ---- */

static void ListSelect(int16_t delta)
{
    int16_t count = (int16_t)PACK_CarCount();
    g_list_sel += delta;
    if (g_list_sel < 0)
        g_list_sel = count;                 /* wrap to the ＋ NEW row */
    if (g_list_sel > count)
        g_list_sel = 0;
    if (g_list_sel < count) {
        const PackCar_t *c = PACK_GetCar((uint8_t)g_list_sel);
        if (c != NULL)
            TuneChannel(c->channel);        /* LIST is a live browse surface */
    }
    gUpdateDisplay = true;
}

static void HoldWalk(int16_t delta)
{
    uint8_t count = PACK_CarCount();
    if (count == 0) {
        Flash("NO CARS");
        return;
    }
    uint16_t current = gRxVfo->CHANNEL_SAVE;
    int16_t idx = -1;
    for (uint8_t i = 0; i < count; i++)
        if (PACK_GetCar(i)->channel == current) {
            idx = (int16_t)i;
            break;
        }
    if (idx < 0)
        idx = (delta > 0) ? -1 : count;     /* start at the ends */
    idx += delta;
    if (idx < 0)
        idx = (int16_t)(count - 1);
    if (idx >= (int16_t)count)
        idx = 0;
    TuneCar(PACK_GetCar((uint8_t)idx));
}

static void JumpFavorites(void)
{
    uint8_t count = PACK_CarCount();
    uint8_t start = 0;

    if (count == 0) {
        Flash("NO FAVS");
        return;
    }
    for (uint8_t i = 0; i < count; i++)
        if (PACK_GetCar(i)->channel == gRxVfo->CHANNEL_SAVE) {
            start = (uint8_t)((i + 1) % count);
            break;
        }
    for (uint8_t n = 0; n < count; n++) {
        uint8_t i = (uint8_t)((start + n) % count);
        if (PACK_GetCar(i)->favorite) {
            TuneCar(PACK_GetCar(i));
            return;
        }
    }
    Flash("NO FAVS");
}

static void JumpMyDriver(void)
{
    const char *driver = PACK_MyDriver();
    const PackCar_t *car = (driver != NULL && driver[0] != 0) ? CarByNumber(driver) : NULL;

    if (car == NULL) {
        Flash("NO DRIVER");
        return;
    }
    TuneCar(car);
}

/* ---- CAPTURE save (vision §4.5; T7 adds prefill-from-air + tone) ---- */

static void SaveCapture(void)
{
    const char *num = SCAN01_TYPE_GetBuffer();

    if (num[0] == 0) {
        Flash("TYPE A NUMBER");
        return;
    }
    if (g_capture_freq == 0) {
        Flash("NO FREQ");
        return;
    }
    PackCar_t car;
    memset(&car, 0, sizeof(car));
    strncpy(car.number, num, 3);
    car.number[3] = 0;
    car.freq_hz = g_capture_freq;
    car.group = PACK_GROUP_A;
    car.narrow = true;
    car.name[0] = 0;                        /* PACK_AddCapture fills "NEW"-ish */
    if (!PACK_AddCapture(&car)) {
        Flash("PACK FULL");
        return;
    }
    char msg[15];
    snprintf(msg, sizeof(msg), "SAVED %s", car.number);
    Flash(msg);
    SCAN01_TYPE_Reset();
    StartScan();
}

/* ---- committed typing (vision §4.3) ---- */

static void HandleCommit(const char *entry)
{
    if (strchr(entry, '.') != NULL) {
        uint32_t hz;
        if (!ParseFreq(entry, &hz)) {
            Flash("BAD FREQ");
            return;
        }
        if (!PACK_FreqAllowed(hz, PACK_IsPractice() ? PACK_MODE_PRACTICE : PACK_MODE_RACE)) {
            Flash("BAND LOCKED");
            return;
        }
        TuneFreq(hz);
        SetState(S1_ST_HOLD);
        return;
    }
    const PackCar_t *car = CarByNumber(entry);
    if (car == NULL) {
        char msg[15];
        snprintf(msg, sizeof(msg), "NO CAR %s", entry);
        Flash(msg);
        return;
    }
    TuneCar(car);
}

/* ---- key handling ---- */

void SCAN01_UI_ProcessKeys(KEY_Code_t key, bool bKeyPressed, bool bKeyHeld)
{
    if (g_identity) {
        g_identity = false;                 /* any key skips the boot screen */
        gUpdateDisplay = true;
    }

    Scan01Action_t action = SCAN01_KEYS_ProcessKey(key, bKeyPressed, bKeyHeld);

    switch (action) {
    case SCAN01_ACT_HOLD:                   /* PTT short in SCAN */
        if (PACK_IsValid()) {
            CHFRSCANNER_Stop();
            SetState(S1_ST_HOLD);
        } else {
            Flash("NO PACK");
        }
        break;
    case SCAN01_ACT_RESUME:                 /* PTT short in HOLD/LIST */
        if (g_st == S1_ST_LIST && g_list_sel == (int16_t)PACK_CarCount()) {
            /* ＋ NEW row is a button: the big button opens it */
            g_capture_freq = gRxVfo->freq_config_RX.Frequency;
            SetState(S1_ST_CAPTURE);
        } else {
            StartScan();
        }
        break;
    case SCAN01_ACT_CAPTURE:                /* long-PTT: catch it from the air */
        g_capture_freq = gRxVfo->freq_config_RX.Frequency;
        SCAN01_TYPE_Reset();
        SetState(S1_ST_CAPTURE);
        break;
    case SCAN01_ACT_SAVE:
        SaveCapture();
        break;
    case SCAN01_ACT_SCAN:                   /* * or EXIT back to scanning */
        if (g_st != S1_ST_SCAN)
            StartScan();
        break;
    case SCAN01_ACT_HOME:                   /* long-EXIT: LIST in RACE mode */
        if (PACK_IsValid())
            CHFRSCANNER_Stop();             /* browse is quiet */
        g_list_sel = 0;
        SetState(S1_ST_LIST);
        break;
    case SCAN01_ACT_LIST:                   /* UP/DOWN in SCAN: open LIST */
        if (PACK_IsValid())
            CHFRSCANNER_Stop();
        g_list_sel = 0;
        SetState(S1_ST_LIST);
        break;
    case SCAN01_ACT_NAV_UP:
        if (g_st == S1_ST_LIST)
            ListSelect(-1);
        else if (g_st == S1_ST_HOLD)
            HoldWalk(-1);
        break;
    case SCAN01_ACT_NAV_DOWN:
        if (g_st == S1_ST_LIST)
            ListSelect(+1);
        else if (g_st == S1_ST_HOLD)
            HoldWalk(+1);
        break;
    case SCAN01_ACT_LOCKOUT: {              /* long-* in HOLD/LIST */
        uint8_t idx = 0;
        bool found = false;
        for (uint8_t i = 0; i < PACK_CarCount(); i++) {
            bool is_sel = (g_st == S1_ST_LIST) ? ((int16_t)i == g_list_sel)
                                               : (PACK_GetCar(i)->channel == gRxVfo->CHANNEL_SAVE);
            if (is_sel) {
                idx = i;
                found = true;
                break;
            }
        }
        if (found && (g_st != S1_ST_LIST || g_list_sel < (int16_t)PACK_CarCount())) {
            const uint8_t *bitmap = PACK_LockoutBitmap();
            bool locked = (bitmap[idx >> 3] & (1u << (idx & 7))) != 0;
            PACK_SaveLockout(idx, !locked);
            gUpdateDisplay = true;
        }
        break;
    }
    case SCAN01_ACT_FAVORITES:
        JumpFavorites();
        break;
    case SCAN01_ACT_MYDRIVER:
        JumpMyDriver();
        break;
    case SCAN01_ACT_BRD:
        Flash("BRD T6B");
        break;
    case SCAN01_ACT_WX:
        Flash("WX T6B");
        break;
    case SCAN01_ACT_MUTE:
    case SCAN01_ACT_MUTE_TOGGLE:
        Flash("MUTE T6B");
        break;
    case SCAN01_ACT_GROUP:
        Flash("GROUPS P2");
        break;
    case SCAN01_ACT_SETUP:
    case SCAN01_ACT_KEYLOCK:
        Flash("SETUP T6B");
        break;
    case SCAN01_ACT_BACK:
        StartScan();
        break;
    case SCAN01_ACT_TYPE_UPDATE:
        gUpdateDisplay = true;
        break;
    default:
        break;
    }

    char entry[16];
    if (SCAN01_KEYS_PollCommit(entry, sizeof(entry)))
        HandleCommit(entry);
}

void SCAN01_UI_Tick10ms(void)
{
    if (g_identity_10ms > 0) {
        if (--g_identity_10ms == 0) {
            g_identity = false;
            gUpdateDisplay = true;
        }
    }
    if (!g_identity && g_st == S1_ST_SCAN && gScanStateDir == SCAN_OFF && PACK_IsValid())
        StartScan();                        /* the SCAN state must actually scan; note this
                                             * overrides SCAN_RESUME_MODE=0 (stop on carrier)
                                             * — the edition always resumes scanning */

    if (g_flash_10ms > 0)
        if (--g_flash_10ms == 0)
            gUpdateDisplay = true;
    SCAN01_KEYS_Tick10ms();
}

/* ---- rendering ---- */

static void DrawGlyph(uint8_t row, uint8_t x, const uint8_t *glyph)
{
    for (uint8_t i = 0; i < 8; i++)
        gFrameBuffer[row][x + i] = glyph[i];
}

/* The base's small-string helpers neither clip nor bounds-check (7 px
 * advance, no OOB guard). Every call here goes through these wrappers:
 * 16 chars centered (112 px) or 15 chars left-aligned at x — always safe. */
#define SMALL_CENTER_MAX 16
#define SMALL_LEFT_MAX   15

static void PrintSmall(uint8_t line, const char *text, bool center)
{
    char buf[SMALL_CENTER_MAX + 1];
    strncpy(buf, text, SMALL_CENTER_MAX);
    buf[SMALL_CENTER_MAX] = 0;
    if (center)
        UI_PrintStringSmallNormal(buf, 0, 127, line);
    else
        UI_PrintStringSmallNormal(buf, 0, 0, line);
}

static void PrintSmallAt(uint8_t line, uint8_t x, const char *text)
{
    char buf[SMALL_LEFT_MAX + 1];
    strncpy(buf, text, SMALL_LEFT_MAX);
    buf[SMALL_LEFT_MAX] = 0;
    UI_PrintStringSmallNormal(buf, x, x, line);
}

static uint8_t RssiLevel(void)
{
    int16_t rssi = BK4819_GetRSSI();
    if (rssi >= gEEPROM_RSSI_CALIB[gRxVfo->Band][3])
        return 6;
    if (rssi >= gEEPROM_RSSI_CALIB[gRxVfo->Band][2])
        return 4;
    if (rssi >= gEEPROM_RSSI_CALIB[gRxVfo->Band][1])
        return 2;
    if (rssi >= gEEPROM_RSSI_CALIB[gRxVfo->Band][0])
        return 1;
    return 0;
}

static void DrawSignalBar(uint8_t level)
{
    for (uint8_t i = 0; i < level; i++) {
        uint8_t x = 102 + i * 3;
        gFrameBuffer[5][x] |= 0x1E;         /* 4 px tall, row 5 strip */
        gFrameBuffer[5][x + 1] |= 0x1E;
    }
}

static void RenderListen(void)
{
    const PackCar_t *car = CarByChannel(gRxVfo->CHANNEL_SAVE);
    const PackStation_t *station = (car == NULL) ? StationByChannel(gRxVfo->CHANNEL_SAVE) : NULL;
    char freq[16];
    char state[15];

    UI_DisplayStatus();
    UI_DisplayClear();

    /* number zone: 32 px, right-aligned (vision §5.3) — gFrameBuffer lines 0-3 */
    if (car != NULL)
        RACING_PrintNumber(gFrameBuffer, 0, 127, car->number);
    else if (station != NULL)
        RACING_PrintNumber(gFrameBuffer, 0, 127, station->name);

    /* name 16 px (left), team 8 px */
    if (car != NULL) {
        UI_PrintString(car->name, 0, 0, 1, 7);
        PrintSmallAt(2, 0, car->team);
    } else if (station != NULL) {
        UI_PrintString(station->name, 0, 0, 1, 7);
        PrintSmallAt(2, 0, KIND_TEXT[station->kind & 7]);
    }

    /* freq 8 px (left) + signal bar (right) */
    FormatFreq(freq, gRxVfo->freq_config_RX.Frequency);
    UI_PrintStringSmallBold(freq, 0, 0, 4);      /* ≤ 11 chars, safe */
    DrawSignalBar(RssiLevel());

    /* state line (or a flash) — left-aligned after the glyph */
    if (g_flash_10ms > 0) {
        PrintSmallAt(5, 9, g_flash);
    } else if (g_st == S1_ST_HOLD) {
        snprintf(state, sizeof(state), "HOLD %s", (car != NULL) ? car->number : (station != NULL ? station->name : ""));
        DrawGlyph(5, 0, GLYPH_HOLD);
        PrintSmallAt(5, 9, state);
    } else {
        DrawGlyph(5, 0, GLYPH_SCAN);
        PrintSmallAt(5, 9, "SCAN");
    }

    ST7565_BlitFullScreen();
}

static void RenderList(void)
{
    uint8_t count = PACK_CarCount();
    char num[4];
    char line[24];

    UI_DisplayStatus();
    UI_DisplayClear();

    /* three 16 px rows: sel-1, sel, sel+1 (vision §5.4); selection inverted */
    for (int r = 0; r < 3; r++) {
        int16_t idx = g_list_sel - 1 + r;
        uint8_t row = (uint8_t)(r * 2);
        bool selected = (idx == g_list_sel);

        if (idx < 0 || idx > (int16_t)count)
            continue;
        if (selected) {
            for (uint8_t x = 0; x < 128; x++) {
                gFrameBuffer[row][x] ^= 0xFF;
                gFrameBuffer[row + 1][x] ^= 0xFF;
            }
        }
        if (idx == (int16_t)count) {
            UI_PrintString("+ NEW", 0, 0, row, 7);
            continue;
        }
        const PackCar_t *car = PACK_GetCar((uint8_t)idx);
        if (car == NULL)
            continue;
        strncpy(num, car->number, 3);
        num[3] = 0;
        UI_PrintString(num, 0, 0, row, 7);
        PrintSmallAt(row + 1, 20, car->name);        /* wireframe: name only */
        if (CarLocked(car))
            UI_DrawLineBuffer(gFrameBuffer, 20, (int16_t)((row + 1) * 8 + 8), 127, (int16_t)((row + 1) * 8 + 8), 1);
    }

    snprintf(line, sizeof(line), "%u cars · * lock", (unsigned)count);
    PrintSmall(6, line, false);

    ST7565_BlitFullScreen();
}

static void RenderCapture(void)
{
    char freq[16];
    char preview[20];
    const char *num = SCAN01_TYPE_GetBuffer();

    UI_DisplayStatus();
    UI_DisplayClear();

    /* freq line, 10×16 gFontBigDigits, centered (vision §5.9) */
    FormatFreq(freq, g_capture_freq);
    uint8_t len = (uint8_t)strlen(freq);
    uint8_t x = (len >= 9) ? 0 : (uint8_t)((128 - len * 13) / 2);
    UI_DisplayFrequency(freq, x, 0, false);

    /* number input, 32 px racing digits, right-aligned (lines 2-5) */
    RACING_PrintNumber(gFrameBuffer, 2, 127, num);

    /* the last line is the affordance: preview what PTT will save */
    if (num[0] == 0) {
        PrintSmall(6, "TYPE 1-3 DIGITS", true);
    } else {
        snprintf(preview, sizeof(preview), "SAVE AS CAR #%s", num);
        PrintSmall(6, preview, true);
    }

    ST7565_BlitFullScreen();
}

static void RenderIdentity(void)
{
    char line[24];

    UI_DisplayStatus();
    UI_DisplayClear();

    if (!PACK_IsValid()) {
        UI_PrintString("NO PACK", 0, 127, 1, 7);
        PrintSmall(3, "plug in and load", true);
    } else {
        snprintf(line, 19, "%s — %s", PACK_Track(), PACK_Session());   /* ≤ 18 for 7px centering */
        UI_PrintString(line, 0, 127, 1, 7);
        snprintf(line, sizeof(line), "%u cars · %s", (unsigned)PACK_CarCount(), PACK_Series());
        PrintSmall(3, line, true);
    }

    ST7565_BlitFullScreen();
}

void UI_DisplayScan01(void)
{
    if (g_identity) {
        RenderIdentity();
        return;
    }
    switch (g_st) {
    case S1_ST_SCAN:
    case S1_ST_HOLD:
        RenderListen();
        break;
    case S1_ST_LIST:
        RenderList();
        break;
    case S1_ST_CAPTURE:
        RenderCapture();
        break;
    default:
        break;
    }
}

void SCAN01_UI_Init(void)
{
    g_st = S1_ST_SCAN;
    g_list_sel = 0;
    g_flash[0] = 0;
    g_flash_10ms = 0;
    g_capture_freq = 0;
    g_identity = true;
    g_identity_10ms = 80;                   /* 0.8 s brand/pack screen (vision §5.8) */
    SCAN01_KEYS_Init();
}
