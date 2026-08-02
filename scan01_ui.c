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
#include "app/fm.h"
#include "driver/bk4819.h"
#include "dcs.h"
#include "driver/eeprom.h"
#include "driver/st7565.h"
#include "external/printf/printf.h"
#include "font_racing.h"
#include "frequencies.h"
#include "misc.h"
#include "pack_bandlock.h"
#include "radio.h"
#include "scan01_edit.h"
#include "scan01_keys.h"
#include "settings.h"
#include "settings_pack.h"
#include "ui/helper.h"
#include "ui/status.h"
#include "helper/battery.h"

/* ---- internal states (a subset of the key layer's; BRD/WX/SETUP = T6b) ---- */
typedef enum {
    S1_ST_SCAN = 0,
    S1_ST_HOLD,
    S1_ST_LIST,
    S1_ST_CAPTURE,
    S1_ST_BRD,
    S1_ST_WX,
    S1_ST_SETUP,
    S1_ST_EDIT,
} S1State_t;

static S1State_t g_st = S1_ST_SCAN;
static int16_t   g_list_sel;          /* 0..PACK_CarCount(); == count = ＋ NEW row */
static uint32_t  g_capture_freq;      /* the frequency the CAPTURE form will save */
static char      g_flash[15];         /* transient state-line message */
static uint16_t  g_flash_10ms;
static bool      g_identity;          /* boot identity / NO PACK screen (vision §5.8) */
static uint16_t  g_identity_10ms;
static uint8_t   g_wx_channel;        /* NOAA channel 0-6 (WX state) */
static uint16_t  g_mute_10ms;         /* F1 short: mute countdown */
static uint8_t   g_mute_seconds;      /* the F1 mute duration (SETUP Audio) */
static uint8_t   g_setup_page;        /* SETUP 0-3: Pack / Audio / Display / Info */
static uint8_t   g_setup_focus;       /* the focused row on the page */
static uint8_t   g_capture_tone_index; /* the decoded tone of the capture freq */
static uint8_t   g_capture_code_type;  /* PACK_CT_* */

/* ---- 8 px state glyphs (◉ ◼ ▸) — hand-drawn, column-major ---- */
static const uint8_t GLYPH_SCAN[8] = { 0x3C, 0x42, 0x99, 0xA5, 0xA5, 0x99, 0x42, 0x3C };
static const uint8_t GLYPH_HOLD[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void PrintSmall(uint8_t line, const char *text, bool center);
static void PrintSmallAt(uint8_t line, uint8_t x, const char *text);

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
    case S1_ST_BRD:     SCAN01_KEYS_SetUiState(SCAN01_UI_BRD); break;
    case S1_ST_WX:      SCAN01_KEYS_SetUiState(SCAN01_UI_WX); break;
    case S1_ST_SETUP:   SCAN01_KEYS_SetUiState(SCAN01_UI_SETUP); break;
    case S1_ST_EDIT:    SCAN01_KEYS_SetUiState(SCAN01_UI_LIST); break; /* key layer stays LIST */
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

/* ---- volume + mute (vision §4.1: the volume control; stock K5/K6 has no
 * knob — held ▲/▼ is the gesture, SETUP has a slider; v2 restores the knob) ---- */

static void ApplyVolume(void)
{
    if (gEeprom.VOLUME_GAIN > 15)
        gEeprom.VOLUME_GAIN = 15;
    BK4819_WriteRegister(BK4819_REG_48,
        (11u << 12) | (0u << 10) | (gEeprom.VOLUME_GAIN << 4) | (gEeprom.DAC_GAIN << 0));
    gUpdateStatus = true;
}

static void MuteOn(void)
{
    gMute = true;
    gEeprom.VOLUME_GAIN = 0;
    ApplyVolume();
}

static void MuteOff(void)
{
    gMute = false;
    gEeprom.VOLUME_GAIN = gEeprom.VOLUME_GAIN_BACKUP;
    ApplyVolume();
}

static void VolumeChange(int8_t delta)
{
    if (gMute)
        MuteOff();                          /* first nudge unmutes */
    int8_t v = (int8_t)gEeprom.VOLUME_GAIN + delta;
    if (v < 0)
        v = 0;
    if (v > 15)
        v = 15;
    gEeprom.VOLUME_GAIN = (uint8_t)v;
    gEeprom.VOLUME_GAIN_BACKUP = gEeprom.VOLUME_GAIN;
    ApplyVolume();
    SETTINGS_SaveSettings();                /* the power knob cuts power — persist now */
}

/* ---- BRD (broadcast radio, vision §5.10) ---- */

static uint8_t BrdPresetCount(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < PACK_StationCount(); i++)
        if (PACK_GetStation(i)->kind == PACK_KIND_BROADCAST)
            n++;
    return n;
}

static const PackStation_t *BrdPreset(uint8_t index)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < PACK_StationCount(); i++)
        if (PACK_GetStation(i)->kind == PACK_KIND_BROADCAST) {
            if (n == index)
                return PACK_GetStation(i);
            n++;
        }
    return NULL;
}

static void EnterBrd(void)
{
    uint8_t count = BrdPresetCount();
    CHFRSCANNER_Stop();
    FM_Start();
    if (count > 0) {
        const PackStation_t *st = BrdPreset(0);
        FM_Tune((uint16_t)(st->freq_hz / 100000u), 0, true);
    } else {
        FM_Tune(1011, 0, true);             /* 101.1 — a default that always exists */
        Flash("NO PRESETS");
    }
    SetState(S1_ST_BRD);
}

static void BrdTeardown(void)
{
    if (gFmRadioMode)
        FM_TurnOff();                       /* every BRD exit must clear the FM path */
}

static void ExitBrd(void)
{
    BrdTeardown();
    StartScan();                            /* RADIO_SelectVfos re-arms the RX audio path */
}

static void BrdWalk(int8_t delta)
{
    uint8_t count = BrdPresetCount();
    if (count == 0) {
        Flash("NO PRESETS");
        return;
    }
    uint8_t cur = 0;
    uint32_t cur_hz = (uint32_t)gEeprom.FM_FrequencyPlaying * 100000u;
    for (uint8_t i = 0; i < count; i++)
        if (BrdPreset(i)->freq_hz == cur_hz) {
            cur = i;
            break;
        }
    cur = (uint8_t)((cur + count + delta) % count);
    FM_Tune((uint16_t)(BrdPreset(cur)->freq_hz / 100000u), 0, true);
    gUpdateDisplay = true;
}

/* ---- WX (weather, vision §5.10) ---- */

static void EnterWx(void)
{
    BrdTeardown();                          /* never enter WX with FM audio routed */
    CHFRSCANNER_Stop();
    g_wx_channel = 0;
    TuneChannel((uint16_t)(NOAA_CHANNEL_FIRST + g_wx_channel));
    SetState(S1_ST_WX);
}

static void WxWalk(int8_t delta)
{
    g_wx_channel = (uint8_t)((g_wx_channel + 7 + delta) % 7);
    TuneChannel((uint16_t)(NOAA_CHANNEL_FIRST + g_wx_channel));
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
    if (PACK_IsSealed()) {
        Flash("SEALED");
        return;
    }
    PackCar_t car;
    memset(&car, 0, sizeof(car));
    strncpy(car.number, num, 3);
    car.number[3] = 0;
    car.freq_hz = g_capture_freq;
    car.tone_index = g_capture_tone_index;
    car.code_type = g_capture_code_type;
    car.group = PACK_GROUP_A;
    car.narrow = true;
    /* the name: "NEW" by default; an ALT duplicate inherits the original's
     * name so the LIST reads "ALT BYRON W" (vision §4.5) */
    {
        const PackCar_t *dup = CarByNumber(car.number);
        if (dup != NULL)
            strncpy(car.name, dup->name, 10), car.name[10] = 0;
        else
            strcpy(car.name, "NEW");
    }
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

/* ---- CAPTURE tone (vision §5.9: the tone shows as confirmation of what
 * gets saved — the BK4819's live decode of the current signal) ---- */

static void CaptureRefreshTone(void)
{
    uint32_t cdcss_freq = 0;
    uint16_t ctcss_freq = 0;
    BK4819_CssScanResult_t result = BK4819_GetCxCSSScanResult(&cdcss_freq, &ctcss_freq);

    g_capture_tone_index = 0;
    g_capture_code_type = PACK_CT_NONE;
    if (result == BK4819_CSS_RESULT_CTCSS) {
        uint8_t code = DCS_GetCtcssCode((int)ctcss_freq);
        if (code != 0xFF) {
            g_capture_tone_index = code;
            g_capture_code_type = PACK_CT_CTCSS;
        }
    } else if (result == BK4819_CSS_RESULT_CDCSS) {
        uint8_t code = DCS_GetCdcssCode(cdcss_freq);
        if (code != 0xFF) {
            g_capture_tone_index = code;
            g_capture_code_type = PACK_CT_DCS;
        }
    }
}

/* ---- SETUP (vision §5.5: four boring pages; M = next page, held ▲▼ edits) ---- */

#define SETUP_ROWS 5
#define SETUP_PAGES 4

static void SetupPersist(void)
{
    SETTINGS_SaveSettings();
}

static void SetupValueChange(int8_t delta)
{
    switch (g_setup_page) {
    case 0:                             /* Pack */
        if (g_setup_focus == 0) {       /* Mode: RACE/PRACTICE */
            PACK_SetPractice(!PACK_IsPractice());
            StartScan();                /* the mode change re-arms the scan */
        } else if (g_setup_focus == 1) { /* Seal */
            PACK_SetSealed(!PACK_IsSealed());
            Flash(PACK_IsSealed() ? "SEALED" : "UNSEALED");
        } else if (g_setup_focus == 2) { /* My driver: cycle the car numbers */
            uint8_t count = PACK_CarCount();
            if (PACK_IsSealed()) {
                Flash("SEALED");
            } else if (count > 0) {
                const char *cur = PACK_MyDriver();
                for (uint8_t i = 0; i < count; i++) {
                    uint8_t j = (uint8_t)((i + (delta > 0 ? 1 : count - 1)) % count);
                    const PackCar_t *c = PACK_GetCar(j);
                    if (c != NULL && (cur == NULL || cur[0] == 0 || strcmp(cur, c->number) != 0)) {
                        PACK_SetMyDriver(c->number);
                        break;
                    }
                }
            } else {
                Flash("NO CARS");
            }
        }
        break;
    case 1:                             /* Audio */
        if (g_setup_focus == 0) {       /* Volume */
            if (gMute)
                MuteOff();                  /* never persist a muted 0 */
            int8_t v = (int8_t)gEeprom.VOLUME_GAIN + delta;
            if (v < 0) v = 0;
            if (v > 15) v = 15;
            gEeprom.VOLUME_GAIN = (uint8_t)v;
            gEeprom.VOLUME_GAIN_BACKUP = gEeprom.VOLUME_GAIN;
            ApplyVolume();
            SetupPersist();
        } else if (g_setup_focus == 1) { /* Beeps */
            gEeprom.BEEP_CONTROL = (uint8_t)(!gEeprom.BEEP_CONTROL);
            SetupPersist();
        } else if (g_setup_focus == 2) { /* Mute duration: 3/5/10/30 s */
            static const uint8_t opts[4] = { 3, 5, 10, 30 };
            for (uint8_t i = 0; i < 4; i++)
                if (opts[i] == g_mute_seconds) {
                    g_mute_seconds = opts[(i + 4 + (delta > 0 ? 1 : -1)) % 4];
                    break;
                }
        }
        break;
    case 2:                             /* Display */
        if (g_setup_focus == 0) {       /* Contrast 0-15 */
            int8_t v = (int8_t)gSetting_set_ctr + delta;
            if (v < 0) v = 0;
            if (v > 15) v = 15;
            gSetting_set_ctr = (uint8_t)v;
            ST7565_ContrastAndInv();
            SetupPersist();                 /* SETTINGS_SaveSettings writes 0x1FF0 */
        } else if (g_setup_focus == 1) { /* Invert */
            gSetting_set_inv = !gSetting_set_inv;
            ST7565_ContrastAndInv();
            SetupPersist();
        } else if (g_setup_focus == 2) { /* Backlight: OFF/10/30/60/ALWAYS */
            static const uint8_t opts[5] = { 0, 10, 30, 60, 61 };
            uint8_t cur = 0;
            for (uint8_t i = 0; i < 5; i++)
                if (opts[i] == gEeprom.BACKLIGHT_TIME) {
                    cur = i;
                    break;
                }
            cur = (uint8_t)((cur + 5 + (delta > 0 ? 1 : -1)) % 5);
            gEeprom.BACKLIGHT_TIME = opts[cur];
            SetupPersist();
        }
        break;
    default:
        break;                          /* Info: nothing to edit */
    }
    gUpdateDisplay = true;
}

static void SetupRenderRow(uint8_t line, const char *label, const char *value, bool focus)
{
    char row[24];
    if (value != NULL && value[0] != 0)
        snprintf(row, 17, "%s%s", label, value);
    else
        strncpy(row, label, 16), row[16] = 0;
    if (focus) {
        for (uint8_t x = 0; x < 128; x++)
            gFrameBuffer[line][x] ^= 0xFF;
    }
    PrintSmallAt(line, 0, row);
}

static void RenderSetup(void)
{
    char line[24];
    char num[8];
    bool focus = false;

    UI_DisplayStatus();
    UI_DisplayClear();

    switch (g_setup_page) {
    case 0:                             /* Pack */
        PrintSmall(0, "PACK 1/4", false);
        snprintf(line, sizeof(line), "MODE: %s", PACK_IsPractice() ? "PRACTICE" : "RACE");
        SetupRenderRow(1, line, NULL, g_setup_focus == 0);
        snprintf(line, sizeof(line), "SEAL: %s", PACK_IsSealed() ? "ON" : "OFF");
        SetupRenderRow(2, line, NULL, g_setup_focus == 1);
        snprintf(line, sizeof(line), "DRIVER: %s", (PACK_MyDriver() != NULL && PACK_MyDriver()[0]) ? PACK_MyDriver() : "-");
        SetupRenderRow(3, line, NULL, g_setup_focus == 2);
        snprintf(line, sizeof(line), "%u cars · %u st", (unsigned)PACK_CarCount(), (unsigned)PACK_StationCount());
        SetupRenderRow(4, line, NULL, false);
        snprintf(line, sizeof(line), "%s — %s", PACK_Track(), PACK_Session());
        SetupRenderRow(5, line, NULL, false);
        break;
    case 1:                             /* Audio */
        PrintSmall(0, "AUDIO 2/4", false);
        snprintf(line, sizeof(line), "VOLUME: %u", (unsigned)gEeprom.VOLUME_GAIN);
        SetupRenderRow(1, line, NULL, g_setup_focus == 0);
        snprintf(line, sizeof(line), "BEEPS: %s", gEeprom.BEEP_CONTROL ? "ON" : "OFF");
        SetupRenderRow(2, line, NULL, g_setup_focus == 1);
        snprintf(line, sizeof(line), "MUTE: %us", (unsigned)g_mute_seconds);
        SetupRenderRow(3, line, NULL, g_setup_focus == 2);
        break;
    case 2:                             /* Display */
        PrintSmall(0, "DISPLAY 3/4", false);
        snprintf(line, sizeof(line), "CONTRAST: %u", (unsigned)gSetting_set_ctr);
        SetupRenderRow(1, line, NULL, g_setup_focus == 0);
        snprintf(line, sizeof(line), "INVERT: %s", gSetting_set_inv ? "ON" : "OFF");
        SetupRenderRow(2, line, NULL, g_setup_focus == 1);
        snprintf(line, sizeof(line), "LIGHT: %uS", (unsigned)gEeprom.BACKLIGHT_TIME);
        SetupRenderRow(3, line, NULL, g_setup_focus == 2);
        break;
    default:                            /* Info */
        PrintSmall(0, "INFO 4/4", false);
        SetupRenderRow(1, "SCAN 01 " VERSION_STRING, NULL, false);
        snprintf(num, sizeof(num), "%u%%", (unsigned)BATTERY_VoltsToPercent(gBatteryVoltageAverage));
        snprintf(line, sizeof(line), "BATTERY: %s", num);
        SetupRenderRow(2, line, NULL, false);
        snprintf(line, sizeof(line), "PACK: %s", PACK_Track());
        SetupRenderRow(3, line, NULL, false);
        break;
    }
    PrintSmallAt(6, 0, "hold ▲▼ = edit");
    ST7565_BlitFullScreen();
    (void)focus;
}

/* ---- name editor (multi-tap, vision §4.5) ---- */

static void EnterEdit(void)
{
    if (PACK_IsSealed()) {
        Flash("SEALED");
        return;                             /* a sealed pack cannot be renamed */
    }
    SCAN01_EDIT_Reset();
    SetState(S1_ST_EDIT);
}

static void SaveName(void)
{
    const char *name = SCAN01_EDIT_GetBuffer();
    if (name[0] == 0) {
        Flash("NAME EMPTY");
        return;
    }
    if (PACK_RenameCar((uint8_t)g_list_sel, name)) {
        Flash("RENAMED");
    } else {
        Flash("SEALED");
    }
    SetState(S1_ST_LIST);                   /* any save attempt leaves the editor */
}

static void HandleEditKey(KEY_Code_t key, bool bKeyPressed, bool bKeyHeld)
{
    if (key == KEY_PTT && bKeyPressed) {
        SaveName();
        return;
    }
    if (key == KEY_EXIT && bKeyPressed) {
        if (bKeyHeld)
            SCAN01_EDIT_Clear();
        else
            SCAN01_EDIT_Delete();
        gUpdateDisplay = true;
        return;
    }
    if (SCAN01_EDIT_ProcessKey(key))
        gUpdateDisplay = true;
}

static void RenderEdit(void)
{
    char title[16];
    const PackCar_t *car = PACK_GetCar((uint8_t)g_list_sel);
    const char *buf = SCAN01_EDIT_GetBuffer();

    UI_DisplayStatus();
    UI_DisplayClear();

    if (car != NULL) {
        snprintf(title, sizeof(title), "RENAME %s", car->number);
        PrintSmall(0, title, false);
    }
    UI_PrintString(buf, 0, 127, 1, 7);      /* the name being built, 16 px */
    PrintSmall(4, "2=ABC 3=DEF 9=WXYZ", true);
    PrintSmallAt(5, 0, "PTT = save");
    PrintSmallAt(6, 0, "EXIT = delete");
    ST7565_BlitFullScreen();
}

/* ---- key handling ---- */

void SCAN01_UI_ProcessKeys(KEY_Code_t key, bool bKeyPressed, bool bKeyHeld)
{
    if (g_identity) {
        g_identity = false;                 /* any key skips the boot screen */
        gUpdateDisplay = true;
    }

    if (g_st == S1_ST_EDIT) {
        HandleEditKey(key, bKeyPressed, bKeyHeld);   /* the editor owns every key */
        return;
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
    case SCAN01_ACT_RESUME:                 /* PTT short in HOLD/LIST/BRD/WX */
        if (g_st == S1_ST_BRD) {
            ExitBrd();
            break;
        }
        if (g_st == S1_ST_LIST && g_list_sel == (int16_t)PACK_CarCount()) {
            /* ＋ NEW row is a button: the big button opens it */
            g_capture_freq = gRxVfo->freq_config_RX.Frequency;
            TuneFreq(g_capture_freq);
            CaptureRefreshTone();
            SetState(S1_ST_CAPTURE);
        } else {
            StartScan();
        }
        break;
    case SCAN01_ACT_CAPTURE:                /* long-PTT: catch it from the air */
        if (g_st == S1_ST_BRD) {
            g_capture_freq = (uint32_t)gEeprom.FM_FrequencyPlaying * 100000u;
            FM_TurnOff();                   /* the form must be audible: back to the BK4819 */
        } else if (g_st == S1_ST_WX) {
            g_capture_freq = NoaaFrequencyTable[g_wx_channel];
        } else {
            g_capture_freq = gRxVfo->freq_config_RX.Frequency;
        }
        TuneFreq(g_capture_freq);
        CaptureRefreshTone();               /* the prefill's decoded tone */
        SCAN01_TYPE_Reset();
        SetState(S1_ST_CAPTURE);
        break;
    case SCAN01_ACT_SAVE:
        SaveCapture();
        break;
    case SCAN01_ACT_SCAN:                   /* * or EXIT back to scanning */
        if (g_st == S1_ST_BRD)
            ExitBrd();
        else if (g_st != S1_ST_SCAN)
            StartScan();
        break;
    case SCAN01_ACT_HOME:                   /* long-EXIT: LIST in RACE mode */
        BrdTeardown();                      /* long-EXIT from BRD must clear the FM path */
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
        else if (g_st == S1_ST_BRD)
            BrdWalk(-1);
        else if (g_st == S1_ST_WX)
            WxWalk(-1);
        else if (g_st == S1_ST_SETUP) {
            if (g_setup_focus > 0)
                g_setup_focus--;
            gUpdateDisplay = true;
        }
        break;
    case SCAN01_ACT_NAV_DOWN:
        if (g_st == S1_ST_LIST)
            ListSelect(+1);
        else if (g_st == S1_ST_HOLD)
            HoldWalk(+1);
        else if (g_st == S1_ST_BRD)
            BrdWalk(+1);
        else if (g_st == S1_ST_WX)
            WxWalk(+1);
        else if (g_st == S1_ST_SETUP) {
            if (g_setup_focus < SETUP_ROWS - 1)
                g_setup_focus++;
            gUpdateDisplay = true;
        }
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
        if (g_st != S1_ST_BRD)
            EnterBrd();
        break;
    case SCAN01_ACT_WX:
        if (g_st != S1_ST_WX)
            EnterWx();
        break;
    case SCAN01_ACT_MUTE:                   /* F1 short: the configured mute */
        g_mute_10ms = (uint16_t)(g_mute_seconds * 100u);
        MuteOn();
        break;
    case SCAN01_ACT_MUTE_TOGGLE:            /* F1 long: persistent */
        g_mute_10ms = 0;
        if (gMute)
            MuteOff();
        else
            MuteOn();
        break;
    case SCAN01_ACT_VOL_UP:
        VolumeChange(+1);
        break;
    case SCAN01_ACT_VOL_DOWN:
        VolumeChange(-1);
        break;
    case SCAN01_ACT_GROUP:
        Flash("GROUPS P2");
        break;
    case SCAN01_ACT_SETUP:                  /* M short */
        if (g_st == S1_ST_LIST && g_list_sel < (int16_t)PACK_CarCount()) {
            EnterEdit();                    /* M on a LIST row = rename (vision §4.5) */
        } else {
            BrdTeardown();                  /* M from BRD must clear the FM path */
            g_setup_page = 0;
            g_setup_focus = 0;
            SetState(S1_ST_SETUP);
        }
        break;
    case SCAN01_ACT_SETUP_NEXT:             /* M short in SETUP: next page */
        g_setup_page = (uint8_t)((g_setup_page + 1) % SETUP_PAGES);
        g_setup_focus = 0;
        gUpdateDisplay = true;
        break;
    case SCAN01_ACT_VALUE_UP:
        if (g_st == S1_ST_SETUP)
            SetupValueChange(+1);
        break;
    case SCAN01_ACT_VALUE_DOWN:
        if (g_st == S1_ST_SETUP)
            SetupValueChange(-1);
        break;
    case SCAN01_ACT_KEYLOCK:
        Flash("KEYLOCK T6C");
        break;
    case SCAN01_ACT_BACK:
        if (g_st == S1_ST_SETUP)
            StartScan();
        break;
    case SCAN01_ACT_TYPE_UPDATE:
        if (g_st == S1_ST_CAPTURE)
            CaptureRefreshTone();           /* the tone tracks the live signal */
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
    if (g_mute_10ms > 0)
        if (--g_mute_10ms == 0)
            MuteOff();                      /* the 10 s mute expires */
    SCAN01_EDIT_Tick10ms();                 /* the multi-tap same-key window */
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
    } else if (PACK_IsPractice()) {
        DrawGlyph(5, 0, GLYPH_SCAN);
        PrintSmallAt(5, 9, "PRACTICE");
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

    /* three 16 px rows: sel-1, sel, sel+1 (vision §5.4); selection inverted.
     * Clamp the window at the top so the selection sits in the FIRST block —
     * a selection floating in the middle at the list's top reads as broken. */
    int16_t start = g_list_sel - 1;
    if (start < 0)
        start = 0;
    for (int r = 0; r < 3; r++) {
        int16_t idx = start + r;
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
        /* venue divider: the first venue-1 row shows the second venue's name
         * instead of the car name, with a rule above the row (spec §4.3) */
        if (idx > 0) {
            const PackCar_t *prev = PACK_GetCar((uint8_t)(idx - 1));
            if (prev != NULL && prev->venue != car->venue) {
                char div[15];
                snprintf(div, sizeof(div), "- %s -", PACK_Venue2());
                PrintSmallAt(row + 1, 20, div);
                UI_DrawLineBuffer(gFrameBuffer, 20, (int16_t)((row) * 8), 127, (int16_t)((row) * 8), 1);
            } else {
                PrintSmallAt(row + 1, 20, car->name);   /* wireframe: name only */
            }
        } else {
            PrintSmallAt(row + 1, 20, car->name);
        }
        if (CarLocked(car))
            /* UI_DrawLineBuffer y is content-relative (0 = LCD row 8) */
            UI_DrawLineBuffer(gFrameBuffer, 20, (int16_t)((row + 1) * 8), 127, (int16_t)((row + 1) * 8), 1);
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

    /* the decoded tone, 8 px, on the number zone's left margin — the
     * confirmation of what PTT will save (vision §5.9) */
    if (g_capture_code_type != PACK_CT_NONE) {
        char tone[10];
        if (g_capture_code_type == PACK_CT_CTCSS)
            snprintf(tone, sizeof(tone), "T %u.%u",
                     CTCSS_Options[g_capture_tone_index] / 10,
                     CTCSS_Options[g_capture_tone_index] % 10);
        else
            snprintf(tone, sizeof(tone), "D %03o", DCS_Options[g_capture_tone_index]);
        PrintSmallAt(2, 0, tone);
    }

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

static void RenderBrd(void)
{
    char freq[12];
    char state[16];
    uint8_t count = BrdPresetCount();
    uint8_t cur = 0;
    uint32_t cur_hz = (uint32_t)gEeprom.FM_FrequencyPlaying * 100000u;
    const PackStation_t *st = NULL;

    for (uint8_t i = 0; i < count; i++)
        if (BrdPreset(i)->freq_hz == cur_hz) {
            cur = i;
            st = BrdPreset(i);
            break;
        }
    if (st == NULL && count > 0)
        st = BrdPreset(0);

    UI_DisplayStatus();
    UI_DisplayClear();

    if (st != NULL)
        RACING_PrintNumber(gFrameBuffer, 0, 127, st->name);
    snprintf(freq, sizeof(freq), "%u.%u", gEeprom.FM_FrequencyPlaying / 10,
             gEeprom.FM_FrequencyPlaying % 10);
    UI_PrintStringSmallBold(freq, 0, 0, 4);

    if (g_flash_10ms > 0) {
        PrintSmallAt(5, 9, g_flash);
    } else {
        snprintf(state, sizeof(state), "BRD %u of %u", (unsigned)(cur + 1), (unsigned)count);
        DrawGlyph(5, 0, GLYPH_HOLD);
        PrintSmallAt(5, 9, state);
    }
    PrintSmallAt(6, 0, "PTT = back");
    ST7565_BlitFullScreen();
}

static void RenderWx(void)
{
    char wx[8];
    char freq[12];

    UI_DisplayStatus();
    UI_DisplayClear();

    snprintf(wx, sizeof(wx), "WX %u", (unsigned)(g_wx_channel + 1));
    RACING_PrintNumber(gFrameBuffer, 0, 127, wx);
    FormatFreq(freq, NoaaFrequencyTable[g_wx_channel]);
    UI_PrintStringSmallBold(freq, 0, 0, 4);

    if (g_flash_10ms > 0) {
        PrintSmallAt(5, 9, g_flash);
    } else {
        DrawGlyph(5, 0, GLYPH_HOLD);
        PrintSmallAt(5, 9, "WX NOAA");
    }
    PrintSmallAt(6, 0, "PTT = back");
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
    case S1_ST_BRD:
        RenderBrd();
        break;
    case S1_ST_WX:
        RenderWx();
        break;
    case S1_ST_SETUP:
        RenderSetup();
        break;
    case S1_ST_EDIT:
        RenderEdit();
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
    g_wx_channel = 0;
    g_mute_10ms = 0;
    g_mute_seconds = 10;
    g_setup_page = 0;
    g_setup_focus = 0;
    g_capture_tone_index = 0;
    g_capture_code_type = PACK_CT_NONE;
    SCAN01_KEYS_Init();
}
