/* Scan 01 — headless-radio hardware stubs (the "screenshot without hardware")
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
 * Defines every firmware global/function the Scan 01 UI + helper + pack
 * layers touch, with the radio surface faked: EEPROM is RAM, the LCD blits
 * capture into g_sim_screen, the BK4819/FM/RADIO calls are tracked no-ops.
 * Linked only by tests/sim_radio.c — never into the firmware.
 */

#include <string.h>

#include "app/chFrScanner.h"
#include "app/fm.h"
#include "driver/bk4819.h"
#include "driver/eeprom.h"
#include "driver/st7565.h"
#include "frequencies.h"
#include "misc.h"
#include "ui/inputbox.h"
#include "ui/status.h"
#include "radio.h"
#include "settings.h"

/* ---- the LCD ---- */

uint8_t gFrameBuffer[FRAME_LINES][LCD_WIDTH];
uint8_t gStatusLine[LCD_WIDTH];
uint8_t g_sim_screen[8][LCD_WIDTH];     /* 64x128 bit image, row 0 = status strip */

void ST7565_BlitFullScreen(void)
{
    memcpy(g_sim_screen[1], gFrameBuffer[0], 128);
    memcpy(g_sim_screen[2], gFrameBuffer[1], 128);
    memcpy(g_sim_screen[3], gFrameBuffer[2], 128);
    memcpy(g_sim_screen[4], gFrameBuffer[3], 128);
    memcpy(g_sim_screen[5], gFrameBuffer[4], 128);
    memcpy(g_sim_screen[6], gFrameBuffer[5], 128);
    memcpy(g_sim_screen[7], gFrameBuffer[6], 128);
}

void ST7565_BlitStatusLine(void)
{
    memcpy(g_sim_screen[0], gStatusLine, 128);
}

void ST7565_ContrastAndInv(void)
{
}

/* ---- EEPROM: RAM ---- */

static uint8_t g_sim_eeprom[0x2000];
static bool    g_sim_eeprom_readonly;

void EEPROM_ReadBuffer(uint16_t Address, void *pBuffer, uint8_t Size)
{
    memcpy(pBuffer, g_sim_eeprom + Address, Size);
}

void EEPROM_WriteBuffer(uint16_t Address, const void *pBuffer)
{
    if (g_sim_eeprom_readonly || (uint16_t)Address + 8 > (uint16_t)sizeof(g_sim_eeprom))
        return;                             /* the base guards + no-change returns */
    memcpy(g_sim_eeprom + Address, pBuffer, 8);
}

void SIM_EEPROM_Reset(void)
{
    memset(g_sim_eeprom, 0xFF, sizeof(g_sim_eeprom));
    memset(g_sim_eeprom, 0, 0x50);          /* the base's boot region */
}

void SIM_EEPROM_Get(void *out, uint16_t size)
{
    memcpy(out, g_sim_eeprom, size);
}

void SIM_EEPROM_Read(uint16_t address, void *out, uint16_t size)
{
    memcpy(out, g_sim_eeprom + address, size);
}

void SIM_EEPROM_Poke(uint16_t address, const void *data, uint16_t size)
{
    memcpy(g_sim_eeprom + address, data, size);
}

void SIM_EEPROM_SetReadOnly(bool readonly)
{
    g_sim_eeprom_readonly = readonly;
}

/* ---- ui/helper.c globals ---- */

char gInputBox[8];
uint8_t gInputBoxIndex;

void _putchar(char c)                     /* the mpaland printf sink */
{
    (void)c;
}

/* ---- small firmware helpers the UI touches ---- */

FREQUENCY_Band_t FREQUENCY_GetBand(uint32_t Frequency)
{
    (void)Frequency;
    return BAND6_400MHz;                    /* fine for the screenshots */
}

void UI_DisplayStatus(void)                 /* the strip is stubbed blank */
{
}

/* ---- the radio surface ---- */

bool              gMute;
bool              gEnableSpeaker;
uint16_t          gBatteryVoltageAverage = 7800;
uint16_t          gEEPROM_RSSI_CALIB[7][4];
uint8_t           gUpdateStatus;
bool              gUpdateDisplay;
uint8_t           gSetting_set_ctr = 10;
bool              gSetting_set_inv;

int8_t            gScanStateDir;
bool              gScanKeepResult;

EEPROM_Config_t   gEeprom;
VFO_Info_t       *gRxVfo = &gEeprom.VfoInfo[0];
VFO_Info_t       *gTxVfo = &gEeprom.VfoInfo[1];

const uint32_t    NoaaFrequencyTable[10] = {
    162400000u, 162425000u, 162450000u, 162475000u, 162500000u,
    162525000u, 162550000u, 0, 0, 0
};

void CHFRSCANNER_Start(const bool storeBackupSettings, const int8_t scan_direction)
{
    (void)storeBackupSettings;
    gScanStateDir = scan_direction;
    gScanKeepResult = false;
    /* NOTE: the base starts from the current channel (chFrScanner.c:50);
     * the sim has no scan loop, so it just anchors at channel 0 — the UI
     * only needs a stable CHANNEL_SAVE to render the right car. */
    gRxVfo->CHANNEL_SAVE = 0;
}

void CHFRSCANNER_Stop(void)
{
    gScanStateDir = SCAN_OFF;
    gScanKeepResult = true;                 /* stay on the current channel */
}

void SIM_EEPROM_Read(uint16_t address, void *out, uint16_t size);

void RADIO_ConfigureChannel(const unsigned int VFO, const unsigned int configure)
{
    /* mirror the base: load the channel record (u32 LE Hz at +0) into the VFO */
    VFO_Info_t *pVfo = &gEeprom.VfoInfo[VFO];
    uint8_t ch = gEeprom.ScreenChannel[VFO];
    (void)configure;
    if (ch < 200) {
        uint8_t rec[16];
        SIM_EEPROM_Read((uint16_t)(ch * 16), rec, 16);
        uint32_t hz = (uint32_t)rec[0] | ((uint32_t)rec[1] << 8) |
                      ((uint32_t)rec[2] << 16) | ((uint32_t)rec[3] << 24);
        if (hz != 0xFFFFFFFFu)
            pVfo->freq_config_RX.Frequency = hz;
    }
}

void RADIO_SetupRegisters(const bool isFromSetup)
{
    (void)isFromSetup;
}

void RADIO_ConfigureSquelchAndOutputPower(VFO_Info_t *pVfo)
{
    (void)pVfo;
}

void RADIO_ApplyOffset(VFO_Info_t *pVfo)
{
    (void)pVfo;
}

void RADIO_SelectVfos(void)
{
}

void BK4819_WriteRegister(BK4819_REGISTER_t Register, uint16_t Value)
{
    (void)Register;
    (void)Value;
}

uint16_t BK4819_GetRSSI(void)
{
    return 600;                             /* a strong signal: the bar shows */
}

/* the CSS tone decode: settable, so the CAPTURE tone path is testable */
static BK4819_CssScanResult_t g_sim_css_result = BK4819_CSS_RESULT_NOT_FOUND;
static uint32_t g_sim_cdcss_freq;
static uint16_t g_sim_ctcss_freq;

BK4819_CssScanResult_t BK4819_GetCxCSSScanResult(uint32_t *pCdcssFreq, uint16_t *pCtcssFreq)
{
    *pCdcssFreq = g_sim_cdcss_freq;
    *pCtcssFreq = g_sim_ctcss_freq;
    return g_sim_css_result;
}

void SIM_SetCssResult(BK4819_CssScanResult_t result, uint32_t cdcss, uint16_t ctcss)
{
    g_sim_css_result = result;
    g_sim_cdcss_freq = cdcss;
    g_sim_ctcss_freq = ctcss;
}

bool              gFmRadioMode;

void FM_Start(void)
{
    gFmRadioMode = true;
    gEnableSpeaker = true;
}

void FM_Tune(uint16_t Frequency, int8_t Step, bool bFlag)
{
    (void)Step;
    (void)bFlag;
    gEeprom.FM_FrequencyPlaying = Frequency;
}

void FM_TurnOff(void)
{
    gFmRadioMode = false;
    gEnableSpeaker = false;
}

void AUDIO_AudioPathOn(void)  { }
void AUDIO_AudioPathOff(void) { }

void SETTINGS_SaveSettings(void) { }

unsigned int BATTERY_VoltsToPercent(unsigned int voltage_10mV)
{
    (void)voltage_10mV;
    return 92;
}
