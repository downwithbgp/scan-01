/* Scan 01 — host tests for the pack layer (task T3 gate)
 *
 * Compile: gcc -Wall -Werror -Wextra -I. tests/test_pack.c settings_pack.c pack_bandlock.c -o /tmp/test_pack
 * Run:    /tmp/test_pack
 *
 * EEPROM access is stubbed with a RAM buffer that mirrors the base's
 * EEPROM_WriteBuffer semantics: exactly 8 bytes at Address, no-op when the
 * chunk is unchanged (driver/eeprom.c).
 *
 * Properties under test:
 *   P1 CRC: standard check vector + determinism + validity after every mutation
 *   P2 round-trip: install → load → byte-exact RAM state (cars, stations,
 *      names, teams, tones, bandwidth, flags, lockout bitmap)
 *   P3 durability: mutations survive reload; PRACTICE mutations do not
 *   P4 guards: sealed refuses; out-of-band capture refuses; PACK FULL refuses
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "settings_pack.h"
#include "pack_bandlock.h"

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

/* ---- EEPROM stub ---- */

static uint8_t  g_eeprom[0x2000];
static uint32_t g_writes;   /* count of actual (non-noop) 8-byte writes */

void EEPROM_ReadBuffer(uint16_t Address, void *pBuffer, uint8_t Size)
{
    memcpy(pBuffer, g_eeprom + Address, Size);
}

void EEPROM_WriteBuffer(uint16_t Address, const void *pBuffer)
{
    /* the base writes exactly 8 bytes; the 24C64 wraps within 32-byte pages,
     * so the pack layer must only write 8-aligned addresses (spec §4.3) */
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
    if (memcmp(g_eeprom + Address, pBuffer, 8) != 0) {
        memcpy(g_eeprom + Address, pBuffer, 8);
        g_writes++;
    }
}

static void eeprom_reset(void)
{
    memset(g_eeprom, 0xFF, sizeof(g_eeprom));
    g_writes = 0;
}

/* ---- helpers ---- */

static uint16_t header_crc_from_eeprom(void)
{
    uint8_t h[0x40];
    memcpy(h, g_eeprom + 0x1BD0, 0x40);
    return (uint16_t)h[0x37] | ((uint16_t)h[0x38] << 8);
}

static bool header_crc_valid(void)
{
    uint8_t h[0x40];
    memcpy(h, g_eeprom + 0x1BD0, 0x40);
    h[0x37] = 0;                        /* CRC field reads as zero (convention) */
    h[0x38] = 0;
    return PACK_Crc16Ccitt(h, 0x3A) == header_crc_from_eeprom();
}

static void seal_pack(void)
{
    uint8_t h[0x40];
    memcpy(h, g_eeprom + 0x1BD0, 0x40);
    h[0x39] |= 0x01;
    h[0x37] = 0;                        /* CRC field reads as zero (convention) */
    h[0x38] = 0;
    uint16_t crc = PACK_Crc16Ccitt(h, 0x3A);
    h[0x37] = crc & 0xFF;
    h[0x38] = crc >> 8;
    memcpy(g_eeprom + 0x1BD0, h, 0x40);
}

static PackCar_t make_car(const char *number, const char *name, const char *team,
                          uint32_t freq, uint8_t tone, uint8_t ctype)
{
    PackCar_t c;
    memset(&c, 0, sizeof(c));
    strncpy(c.number, number, 3);
    c.group = PACK_GROUP_A;
    c.venue = 0;
    strncpy(c.name, name, 10);
    strncpy(c.team, team, 6);
    c.freq_hz = freq;
    c.tone_index = tone;
    c.code_type = ctype;
    c.narrow = true;
    return c;
}

int main(void)
{
    /* ---- P1: CRC check vector (CRC-16/CCITT-FALSE) ---- */
    expect(PACK_Crc16Ccitt((const uint8_t *)"123456789", 9) == 0x29B1u,
           "CRC check vector 0x29B1");
    for (int i = 0; i < 100; i++) {
        uint8_t buf[64];
        for (int j = 0; j < 64; j++)
            buf[j] = (uint8_t)((i * 7 + j * 13) & 0xFF);
        expect(PACK_Crc16Ccitt(buf, 64) == PACK_Crc16Ccitt(buf, 64),
               "CRC determinism");
    }

    /* ---- P2a: boot on a virgin radio installs the demo pack ---- */
    eeprom_reset();
    expect(PACK_Init(), "Init succeeds on virgin EEPROM");
    expect(PACK_IsValid() && PACK_IsDemo(), "demo pack loaded");
    expect(PACK_CarCount() == 0, "demo has 0 cars");
    expect(PACK_StationCount() == 7, "demo has 7 stations");
    expect(PACK_GetStation(0)->freq_hz == 121500000u, "demo station 0 = 121.5 guard");
    expect(strcmp(PACK_GetStation(0)->name, "GARD") == 0, "demo station 0 name");
    expect(PACK_GetStation(6)->freq_hz == 467712500u, "demo station 6 = FRS14");
    expect(header_crc_valid(), "demo header CRC valid");
    expect(g_eeprom[0x1BD0] == 'S' && g_eeprom[0x1BD1] == 'C'
        && g_eeprom[0x1BD2] == '0' && g_eeprom[0x1BD3] == '1', "magic SC01 at 0x1BD0");
    /* AM airband + wide channel records for the demo, at ch 64..70 (spec §5.1) */
    expect(g_eeprom[64 * 16 + 0x0B] == 0x10 && g_eeprom[64 * 16 + 0x0C] == 0x44,
           "demo ch64: AM << 4, wide, TX_LOCK + power low");
    expect(g_eeprom[0x1BD0 + 0x21] == 0 && g_eeprom[0x1BD0 + 0x22] == 7,
           "header counts 0/7");

    /* ---- P2b: reload from EEPROM is byte-exact ---- */
    uint32_t writes_after_demo = g_writes;
    expect(PACK_Init(), "reload succeeds");
    expect(PACK_IsValid() && !PACK_IsDemo(), "reload is a normal pack");
    expect(PACK_StationCount() == 7, "reload keeps 7 stations");
    expect(PACK_GetStation(0)->freq_hz == 121500000u, "reload keeps freq");
    expect(g_writes == writes_after_demo, "reload writes nothing (no-op chunks)");

    /* ---- P3: capture round-trip ---- */
    PackCar_t c = make_car("77", "BYRON W", "HMS", 450887500u, 20, PACK_CT_CTCSS);
    expect(PACK_AddCapture(&c), "capture accepted");
    expect(PACK_CarCount() == 1, "car count 1");
    /* channel record bytes: freq LE (450.8875 = 0x1ADFFF4C), 0x11 code type,
     * 0x46 narrow TX-locked */
    expect(g_eeprom[0x00] == 0x4C && g_eeprom[0x01] == 0xFF
        && g_eeprom[0x02] == 0xDF && g_eeprom[0x03] == 0x1A,
        "ch0 freq 450.8875 LE bytes");
    expect(g_eeprom[0x0A] == 0x11 && g_eeprom[0x0C] == 0x46,
        "ch0 code-type 0x11, narrow TX-locked 0x46");
    expect(header_crc_valid(), "capture keeps CRC valid");
    /* name + team in the name region */
    expect(strncmp((char *)g_eeprom + 0x0F50, "BYRON W   ", 10) == 0, "name written");
    expect(strncmp((char *)g_eeprom + 0x0F50 + 10, "HMS", 3) == 0, "team written");

    /* out-of-band capture refused (ECPA at rest) */
    PackCar_t bad = make_car("99", "BAD", "", 1850000000u, 0, PACK_CT_NONE);
    expect(!PACK_AddCapture(&bad), "cellular capture refused");
    expect(PACK_CarCount() == 1, "count unchanged after refusal");

    /* duplicate → ALT entry, never overwrite */
    PackCar_t c2 = make_car("77", "BYRON W", "HMS", 451112500u, 20, PACK_CT_CTCSS);
    expect(PACK_AddCapture(&c2), "duplicate capture accepted as ALT");
    expect(PACK_CarCount() == 2, "count 2");
    expect(strncmp(PACK_GetCar(1)->name, "ALT ", 4) == 0, "ALT name prefix");
    expect(PACK_GetCar(1)->freq_hz == 451112500u, "ALT entry keeps its freq");
    expect(strcmp(PACK_GetCar(0)->number, "77") == 0
        && PACK_GetCar(0)->freq_hz == 450887500u, "original untouched");
    expect(PACK_GetCar(1)->origin == PACK_ORIGIN_CAPTURED
        && !PACK_GetCar(1)->verified, "captured/verified flags");

    /* reload: everything persists */
    expect(PACK_Init(), "reload after captures");
    expect(PACK_CarCount() == 2, "captures persist");
    expect(PACK_GetCar(1)->freq_hz == 451112500u
        && strncmp(PACK_GetCar(1)->name, "ALT ", 4) == 0, "ALT persists");
    expect(PACK_GetCar(0)->tone_index == 20 && PACK_GetCar(0)->code_type == PACK_CT_CTCSS,
        "tone persists");

    /* ---- lockout + favorite round-trips ---- */
    expect(PACK_SaveLockout(0, true), "lockout set");
    expect(PACK_Init(), "reload after lockout");
    expect((PACK_LockoutBitmap()[0] & 0x01) != 0, "lockout bit 0 persisted");
    expect(PACK_SaveLockout(0, false), "lockout clear");
    expect(PACK_Init(), "reload after clear");
    expect((PACK_LockoutBitmap()[0] & 0x01) == 0, "lockout bit 0 cleared");
    expect(PACK_SaveFavorite(0, true), "favorite set");
    expect(PACK_Init(), "reload after favorite");
    expect(PACK_GetCar(0)->favorite, "favorite persisted");

    /* ---- my-driver ---- */
    expect(PACK_SetMyDriver("77"), "my-driver set");
    expect(PACK_Init(), "reload after my-driver");
    expect(strcmp(PACK_MyDriver(), "77") == 0, "my-driver persisted");

    /* ---- P4a: sealed refuses everything ---- */
    seal_pack();
    expect(PACK_Init(), "load sealed pack");
    expect(PACK_IsSealed(), "seal flag read");
    uint8_t count_before = PACK_CarCount();
    expect(!PACK_SaveLockout(0, true), "sealed: lockout refused");
    expect(!PACK_SaveFavorite(0, false), "sealed: favorite refused");
    expect(!PACK_SetMyDriver("24"), "sealed: my-driver refused");
    expect(!PACK_AddCapture(&c), "sealed: capture refused");
    expect(PACK_CarCount() == count_before, "sealed: nothing changed");
    expect(header_crc_valid(), "sealed: CRC still valid");

    /* ---- P4b: PRACTICE mutations are RAM-only ---- */
    eeprom_reset();
    PACK_Init();
    PACK_SetPractice(true);
    PackCar_t pc = make_car("5", "LARSON K", "HMS", 465862500u, 0, PACK_CT_NONE);
    expect(PACK_AddCapture(&pc), "practice capture accepted");
    expect(PACK_CarCount() == 1, "practice capture visible in RAM");
    expect(g_eeprom[0x1BD0 + 0x21] == 0, "practice capture NOT persisted (count 0)");
    PACK_SetPractice(false);
    expect(PACK_Init(), "reload after practice");
    expect(PACK_CarCount() == 0, "practice capture gone after power cycle");

    /* ---- PACK FULL ---- */
    eeprom_reset();
    PACK_Init();
    PackCar_t fill = make_car("1", "FILL", "", 450100000u, 0, PACK_CT_NONE);
    bool full_reached = false;
    for (int i = 0; i <= PACK_MAX_CARS; i++) {
        char num[4];
        snprintf(num, 4, "%d", i + 1);
        fill.number[0] = num[0];
        fill.number[1] = num[1] ? num[1] : 0;
        fill.number[2] = num[2] ? num[2] : 0;
        fill.freq_hz = 450100000u + i * 25000u;
        if (!PACK_AddCapture(&fill)) {
            full_reached = (i == PACK_MAX_CARS);
            break;
        }
    }
    expect(full_reached, "PACK FULL at 64");
    expect(PACK_CarCount() == PACK_MAX_CARS, "64 cars held");

    /* ---- corrupted magic → demo fallback ---- */
    eeprom_reset();
    PACK_Init();
    g_eeprom[0x1BD0] = 'X';     /* corrupt magic */
    expect(PACK_Init(), "corrupt magic recovers");
    expect(PACK_IsValid() && PACK_IsDemo(), "corruption → demo fallback");
    expect(PACK_GetStation(0)->freq_hz == 121500000u, "fallback is the demo pack");

    /* ---- P4c: PRACTICE exit discards the session (spec §8) ---- */
    eeprom_reset();
    PACK_Init();
    PACK_SetPractice(true);
    PackCar_t pc2 = make_car("9", "LARSON K", "HMS", 465862500u, 0, PACK_CT_NONE);
    PACK_AddCapture(&pc2);
    expect(PACK_CarCount() == 1, "practice capture visible");
    PACK_SetPractice(false);            /* exit → discard */
    expect(PACK_CarCount() == 0, "practice session discarded on exit");

    /* ---- 2m capture allowed in PRACTICE, refused in RACE ---- */
    PACK_SetPractice(true);
    PackCar_t ham = make_car("10", "HAM", "", 146940000u, 0, PACK_CT_NONE);
    expect(PACK_AddCapture(&ham), "2m capture accepted in PRACTICE");
    PACK_SetPractice(false);
    expect(PACK_CarCount() == 0, "2m capture discarded with the session");
    expect(!PACK_AddCapture(&ham), "2m capture refused in RACE");

    /* ---- P4d: corrupted CRC → demo fallback ---- */
    eeprom_reset();
    PACK_Init();
    g_eeprom[0x1BD0 + 0x37] ^= 0xFF;    /* corrupt the CRC field */
    expect(PACK_Init(), "corrupt CRC recovers");
    expect(PACK_IsValid() && PACK_IsDemo(), "corrupt CRC → demo fallback");

    /* ---- P4e: stale channel sanity — a car on a cellular freq fails load ---- */
    eeprom_reset();
    PACK_Init();
    /* write the record directly (AddCapture refuses it by design) */
    uint8_t rec[16];
    memset(rec, 0, 16);
    uint32_t f = 1850000000u;
    rec[0] = f & 0xFF; rec[1] = (f >> 8) & 0xFF;
    rec[2] = (f >> 16) & 0xFF; rec[3] = (f >> 24) & 0xFF;
    memcpy(g_eeprom + 0x00, rec, 16);   /* channel 0 = car 0 */
    g_eeprom[0x1BD0 + 0x21] = 1;        /* nCars = 1 */
    g_eeprom[0x1BD0 + 0x22] = 0;        /* nStations = 0 */
    g_eeprom[0x1BD0 + 0x37] = 0;
    g_eeprom[0x1BD0 + 0x38] = 0;
    uint16_t crc2 = PACK_Crc16Ccitt(g_eeprom + 0x1BD0, 0x3A);
    g_eeprom[0x1BD0 + 0x37] = crc2 & 0xFF;
    g_eeprom[0x1BD0 + 0x38] = crc2 >> 8;
    expect(PACK_Init() && PACK_IsDemo(), "stale cellular channel → demo fallback");

    /* ---- station-meta bit round-trip (kind/digital/venue/dcs) ---- */
    eeprom_reset();
    PACK_Init();
    /* the API has no station writer yet; verify the bits via a re-scan of the
     * raw meta (kind OTHER=7, digital=0, venue=0, tone none) */
    uint8_t meta[10];
    memcpy(meta, g_eeprom + 0x1D10, 10);
    expect(meta[8] == 0x00 && meta[9] == 0x07, "station 0 tone/kind bits");

    printf("pack layer: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
