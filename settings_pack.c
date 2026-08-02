/* Scan 01 — pack layer (spec/p1-pack-eeprom §4/§5/§8, task T3)
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
 * NOTE: EEPROM_ReadBuffer/EEPROM_WriteBuffer are provided by the firmware
 * build (driver/eeprom.c) and by the host test harness (tests/test_pack.c).
 * All EEPROM writes are 8-byte chunks at 8-aligned addresses: the base's
 * EEPROM_WriteBuffer writes exactly 8 bytes and the 24C64 wraps within
 * 32-byte pages, so any other alignment silently corrupts memory
 * (spec §4.3 "why the alignment").
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "settings_pack.h"
#include "pack_bandlock.h"

/* ---- EEPROM access (firmware: driver/eeprom.c; host test: RAM stub) ---- */
void EEPROM_ReadBuffer(uint16_t Address, void *pBuffer, uint8_t Size);
void EEPROM_WriteBuffer(uint16_t Address, const void *pBuffer);

/* ---- pack table layout (spec §4.3) ---- */
#define PACK_TABLE_BASE     0x1BD0u
#define PACK_TABLE_MAX      592u            /* 0x1BD0..0x1DFF */
#define PACK_HEADER_SIZE    0x40u           /* 0x00..0x3F */
#define PACK_CRC_LEN        0x3Au           /* CRC over 0x00..0x39 (58 bytes) */
#define PACK_CRC_OFF        0x37u
#define PACK_FLAGS_OFF      0x39u
#define PACK_FLAG_SEALED    0x01u
#define PACK_MAGIC          { 'S', 'C', '0', '1' }
#define PACK_VERSION        0x01u   /* exported via settings_pack.h */

/* ---- RAM state ---- */
#define PACK_STATION_CHANNEL_BASE 64u   /* stations live at ch 64..87 so captures
                                         * (cars at 0..63) never collide (spec §5.1) */
static PackCar_t      g_cars[PACK_MAX_CARS];
static PackStation_t  g_stations[PACK_MAX_STATIONS];
static uint8_t        g_lockout[8];
static uint8_t        g_car_count;
static uint8_t        g_station_count;
static bool           g_valid;
static bool           g_demo;
static bool           g_sealed;
static bool           g_practice;
static char           g_series[9];
static char           g_track[13];
static char           g_session[9];
static char           g_venue2[9];
static char           g_my_driver[5];

/* ---- pure CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection) ---- */
uint16_t PACK_Crc16Ccitt(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000u)
                crc = (crc << 1) ^ 0x1021u;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/* ---- header (64 bytes at PACK_TABLE_BASE) ---- */

static void header_serialize(uint8_t out[PACK_HEADER_SIZE])
{
    static const uint8_t magic[4] = PACK_MAGIC;
    memset(out, 0, PACK_HEADER_SIZE);
    memcpy(out + 0x00, magic, 4);
    out[0x04] = PACK_VERSION;
    memcpy(out + 0x05, g_series, 8);
    memcpy(out + 0x0D, g_track, 12);
    memcpy(out + 0x19, g_session, 8);
    out[0x21] = PACK_CarCount();
    out[0x22] = PACK_StationCount();
    memcpy(out + 0x23, g_lockout, 8);
    memcpy(out + 0x2B, g_my_driver, 4);
    memcpy(out + 0x2F, g_venue2, 8);
    out[PACK_FLAGS_OFF] = g_sealed ? PACK_FLAG_SEALED : 0;
    /* CRC over 0x00..0x39 with the CRC field itself read as zero (standard
     * checksum convention) — written last, over the real flags byte */
    uint16_t crc = PACK_Crc16Ccitt(out, PACK_CRC_LEN);
    out[PACK_CRC_OFF] = crc & 0xFF;
    out[PACK_CRC_OFF + 1] = crc >> 8;
}

static void header_commit(void)
{
    uint8_t header[PACK_HEADER_SIZE];
    header_serialize(header);
    for (uint16_t off = 0; off < PACK_HEADER_SIZE; off += 8)
        EEPROM_WriteBuffer(PACK_TABLE_BASE + off, header + off);
}

/* ---- layout helpers ---- */

static uint16_t car_meta_base(void)
{
    return PACK_TABLE_BASE + PACK_HEADER_SIZE;      /* 8-aligned */
}

/* StationMeta base is FIXED at the end of the car-meta capacity (0x40 + 4·64):
 * a capture that grows the car count must never shift the station region
 * (relocation would clobber stations when nCars crosses an alignment edge). */
static uint16_t station_meta_base(void)
{
    return car_meta_base() + PACK_MAX_CARS * 4;     /* 8-aligned, fixed */
}

static bool budget_fits(uint8_t cars, uint8_t stations)
{
    /* fixed station base: cars (≤ 64) live in 0x40..0x140, stations after */
    if (cars > PACK_MAX_CARS || stations > PACK_MAX_STATIONS)
        return false;
    uint16_t end = station_meta_base() + stations * 10u;
    return (end - PACK_TABLE_BASE) <= PACK_TABLE_MAX;
}

/* ---- CarMeta (4 B) ---- */

static void car_meta_write(uint8_t index, const PackCar_t *car)
{
    uint8_t meta[4];
    uint8_t chunk[8];
    uint16_t c = car_meta_base() + (index / 2) * 8;   /* 8-aligned chunk, 2 records */
    meta[0] = (uint8_t)car->number[0];
    meta[1] = (uint8_t)car->number[1];
    meta[2] = (uint8_t)car->number[2];
    meta[3] = (car->group & 0x03u)
            | (car->favorite ? (1u << 2) : 0)
            | ((car->origin & 0x03u) << 3)
            | (car->verified ? (1u << 5) : 0)
            | ((car->venue & 0x01u) << 6);
    EEPROM_ReadBuffer(c, chunk, 8);                   /* RMW the partner record */
    memcpy(chunk + (index % 2) * 4, meta, 4);
    EEPROM_WriteBuffer(c, chunk);
}

static void car_meta_read(uint8_t index, PackCar_t *car)
{
    uint8_t meta[4];
    EEPROM_ReadBuffer(car_meta_base() + index * 4, meta, 4);
    car->number[0] = (char)meta[0];
    car->number[1] = (char)meta[1];
    car->number[2] = (char)meta[2];
    car->number[3] = 0;
    car->group    = meta[3] & 0x03u;
    car->favorite = (meta[3] & (1u << 2)) != 0;
    car->origin   = (meta[3] >> 3) & 0x03u;
    car->verified = (meta[3] & (1u << 5)) != 0;
    car->venue    = (meta[3] >> 6) & 0x01u;
}

/* ---- StationMeta (10 B: 8-byte chunk + 2-byte RMW tail) ---- */

static void station_meta_write(uint8_t index, const PackStation_t *st)
{
    uint8_t meta[10];
    uint16_t off = index * 10;              /* record offset relative to station base */

    memset(meta, 0, 10);
    memcpy(meta, st->name, 4);
    meta[4] = st->freq_hz & 0xFF;
    meta[5] = (st->freq_hz >> 8) & 0xFF;
    meta[6] = (st->freq_hz >> 16) & 0xFF;
    meta[7] = (st->freq_hz >> 24) & 0xFF;
    meta[8] = (st->tone_is_dcs ? 0x80u : 0) | (st->tone_index & 0x7Fu);
    meta[9] = (st->kind & 0x07u)
            | (st->digital ? (1u << 3) : 0)
            | ((st->venue & 0x01u) << 4);

    /* the 10-byte record spans one or two 8-byte chunks at 8-aligned
     * addresses; RMW each chunk with only the overlapping bytes */
    for (uint16_t b = 0; b < 10; ) {
        uint16_t chunk_off = (off + b) / 8 * 8;
        uint16_t in_chunk = (off + b) % 8;
        uint16_t n = 10 - b;
        uint8_t chunk[8];
        if (n > 8 - in_chunk)
            n = 8 - in_chunk;
        EEPROM_ReadBuffer(station_meta_base() + chunk_off, chunk, 8);
        memcpy(chunk + in_chunk, meta + b, n);
        EEPROM_WriteBuffer(station_meta_base() + chunk_off, chunk);
        b += n;
    }
}

static void station_meta_read(uint8_t index, PackStation_t *st)
{
    uint8_t meta[10];
    uint16_t addr = station_meta_base() + index * 10;

    EEPROM_ReadBuffer(addr, meta, 8);
    EEPROM_ReadBuffer(addr + 8, meta + 8, 2);

    memcpy(st->name, meta, 4);
    st->name[4] = 0;
    st->freq_hz = (uint32_t)meta[4]
                | ((uint32_t)meta[5] << 8)
                | ((uint32_t)meta[6] << 16)
                | ((uint32_t)meta[7] << 24);
    st->tone_index   = meta[8] & 0x7Fu;
    st->tone_is_dcs  = (meta[8] & 0x80u) != 0;
    st->kind    = meta[9] & 0x07u;
    st->digital = (meta[9] & (1u << 3)) != 0;
    st->venue   = (meta[9] >> 4) & 0x01u;
}

/* ---- channel records (16 B at ch*16, base format, spec §4.1) ---- */

static void channel_record_write(uint16_t channel, uint32_t freq_hz,
                                 uint8_t tone_index, uint8_t code_type,
                                 bool narrow, bool am)
{
    uint8_t rec[16];
    memset(rec, 0, 16);
    rec[0] = freq_hz & 0xFF;
    rec[1] = (freq_hz >> 8) & 0xFF;
    rec[2] = (freq_hz >> 16) & 0xFF;
    rec[3] = (freq_hz >> 24) & 0xFF;
    rec[8] = tone_index;
    rec[9] = tone_index;
    rec[10] = (uint8_t)((code_type << 4) | code_type);   /* 0x11 CTCSS, 0x22 DCS */
    rec[11] = (uint8_t)(am ? 0x10u : 0u);                /* AM << 4, offset off */
    rec[12] = 0x40u | (1u << 2)                          /* TX_LOCK | power low */
            | (narrow ? (1u << 1) : 0);                  /* 0x46 narrow, 0x44 wide */
    EEPROM_WriteBuffer(channel * 16, rec);
    EEPROM_WriteBuffer(channel * 16 + 8, rec + 8);
}

static void channel_record_read(uint16_t channel, uint32_t *freq_hz,
                                uint8_t *tone_index, uint8_t *code_type,
                                bool *narrow)
{
    uint8_t rec[16];
    EEPROM_ReadBuffer(channel * 16, rec, 16);
    *freq_hz = (uint32_t)rec[0]
             | ((uint32_t)rec[1] << 8)
             | ((uint32_t)rec[2] << 16)
             | ((uint32_t)rec[3] << 24);
    *tone_index = rec[8];
    *code_type = rec[10] & 0x0F;
    *narrow = (rec[12] & (1u << 1)) != 0;
}

/* ---- names + team (0x0F50 + ch*16: 10 chars + 6-byte team) ---- */

#define NAME_BASE 0x0F50u

static void name_team_write(uint16_t channel, const char *name, const char *team)
{
    uint8_t buf[16];
    memset(buf, ' ', 16);
    for (int i = 0; i < 10 && name[i] != 0; i++)
        buf[i] = (uint8_t)name[i];
    for (int i = 0; i < 6 && team[i] != 0; i++)
        buf[10 + i] = (uint8_t)team[i];
    EEPROM_WriteBuffer(NAME_BASE + channel * 16, buf);
    EEPROM_WriteBuffer(NAME_BASE + channel * 16 + 8, buf + 8);
}

static void name_team_read(uint16_t channel, char *name, char *team)
{
    uint8_t buf[16];
    EEPROM_ReadBuffer(NAME_BASE + channel * 16, buf, 16);
    int i;
    for (i = 0; i < 10 && buf[i] >= 32 && buf[i] <= 127; i++)
        name[i] = (char)buf[i];
    name[i] = 0;
    while (i > 0 && name[i - 1] == ' ')
        name[--i] = 0;
    for (i = 0; i < 6 && buf[10 + i] >= 32 && buf[10 + i] <= 127; i++)
        team[i] = (char)buf[10 + i];
    team[i] = 0;
    while (i > 0 && team[i - 1] == ' ')
        team[--i] = 0;
}

/* ---- demo pack (flash-resident daily presets, spec §8) ---- */

static const PackStation_t DEMO_STATIONS[] = {
    /* names are hand-truncated to the 4-char binary limit; the library
     * JSON (race-packs/library/daily/daily-presets.json) has the full ones */
    { "GARD", 121500000u, 0, false, PACK_KIND_OTHER, false, 0, 0 },
    { "UNIC", 122800000u, 0, false, PACK_KIND_OTHER, false, 0, 1 },
    { "MA16", 156800000u, 0, false, PACK_KIND_OTHER, false, 0, 2 },
    { "MA09", 156450000u, 0, false, PACK_KIND_OTHER, false, 0, 3 },
    { "F1",   462562500u, 0, false, PACK_KIND_OTHER, false, 0, 4 },
    { "F7",   462712500u, 0, false, PACK_KIND_OTHER, false, 0, 5 },
    { "F14",  467712500u, 0, false, PACK_KIND_OTHER, false, 0, 6 },
};
#define DEMO_STATION_COUNT (sizeof(DEMO_STATIONS) / sizeof(DEMO_STATIONS[0]))

static void pack_install_demo(void)
{
    uint8_t n;
    memset(g_lockout, 0, 8);
    g_sealed = false;
    g_car_count = 0;
    g_station_count = (uint8_t)DEMO_STATION_COUNT;
    strcpy(g_series, "SCAN01  ");
    strcpy(g_track, "DAILY   ");
    strcpy(g_session, "DEMO    ");
    memset(g_venue2, ' ', 8); g_venue2[8] = 0;
    memset(g_my_driver, 0, 4); g_my_driver[4] = 0;
    g_demo = true;

    for (n = 0; n < DEMO_STATION_COUNT; n++) {
        g_stations[n] = DEMO_STATIONS[n];
        g_stations[n].channel = PACK_STATION_CHANNEL_BASE + n;
        bool am = g_stations[n].freq_hz >= 108000000u && g_stations[n].freq_hz < 137000000u;
        channel_record_write(g_stations[n].channel, g_stations[n].freq_hz, 0, PACK_CT_NONE, false, am);
        name_team_write(g_stations[n].channel, g_stations[n].name, "");
        station_meta_write(n, &g_stations[n]);
    }
    header_commit();
}

/* ---- load ---- */

static void load_strings(const uint8_t *header)
{
    int i;
    for (i = 0; i < 8 && header[0x05 + i] >= 32 && header[0x05 + i] <= 127; i++)
        g_series[i] = (char)header[0x05 + i];
    g_series[i] = 0;
    for (i = 0; i < 12 && header[0x0D + i] >= 32 && header[0x0D + i] <= 127; i++)
        g_track[i] = (char)header[0x0D + i];
    g_track[i] = 0;
    for (i = 0; i < 8 && header[0x19 + i] >= 32 && header[0x19 + i] <= 127; i++)
        g_session[i] = (char)header[0x19 + i];
    g_session[i] = 0;
    for (i = 0; i < 8 && header[0x2F + i] >= 32 && header[0x2F + i] <= 127; i++)
        g_venue2[i] = (char)header[0x2F + i];
    g_venue2[i] = 0;
    for (i = 0; i < 4 && header[0x2B + i] >= 32 && header[0x2B + i] <= 127; i++)
        g_my_driver[i] = (char)header[0x2B + i];
    g_my_driver[i] = 0;
}

bool PACK_Load(void)
{
    static const uint8_t magic[4] = PACK_MAGIC;
    uint8_t header[PACK_HEADER_SIZE];
    uint8_t cars, stations;
    uint8_t i;

    EEPROM_ReadBuffer(PACK_TABLE_BASE, header, PACK_HEADER_SIZE);

    if (memcmp(header, magic, 4) != 0 || header[0x04] != PACK_VERSION)
        return false;

    uint16_t crc = (uint16_t)header[PACK_CRC_OFF] | ((uint16_t)header[PACK_CRC_OFF + 1] << 8);
    uint8_t hdr[PACK_HEADER_SIZE];
    memcpy(hdr, header, PACK_HEADER_SIZE);
    hdr[PACK_CRC_OFF] = 0;              /* CRC field reads as zero (convention) */
    hdr[PACK_CRC_OFF + 1] = 0;
    if (PACK_Crc16Ccitt(hdr, PACK_CRC_LEN) != crc)
        return false;

    cars = header[0x21];
    stations = header[0x22];
    if (cars > PACK_MAX_CARS || stations > PACK_MAX_STATIONS || !budget_fits(cars, stations))
        return false;

    g_car_count = cars;
    g_station_count = stations;

    load_strings(header);
    memcpy(g_lockout, header + 0x23, 8);
    g_sealed = (header[PACK_FLAGS_OFF] & PACK_FLAG_SEALED) != 0;

    for (i = 0; i < cars; i++) {
        PackCar_t *car = &g_cars[i];
        uint32_t freq;
        uint8_t tone, ctype;
        bool narrow;
        car_meta_read(i, car);
        car->channel = i;
        channel_record_read(i, &freq, &tone, &ctype, &narrow);
        car->freq_hz = freq;
        car->tone_index = tone;
        car->code_type = ctype;
        car->narrow = narrow;
        name_team_read(i, car->name, car->team);
        /* sanity: a stale header must not resurrect a broken pack (§4.4) */
        if (!PACK_FreqAllowed(freq, PACK_MODE_RACE))
            return false;
    }

    for (i = 0; i < stations; i++) {
        PackStation_t *st = &g_stations[i];
        station_meta_read(i, st);
        if (st->kind == PACK_KIND_BROADCAST)
            st->channel = 0xFFFF;
        else {
            st->channel = PACK_STATION_CHANNEL_BASE + i;
            if (!PACK_FreqAllowed(st->freq_hz, PACK_MODE_RACE))
                return false;
        }
    }

    g_valid = true;
    g_practice = false;
    return true;
}

bool PACK_Init(void)
{
    g_valid = false;
    g_demo = false;
    if (PACK_Load())
        return true;
    pack_install_demo();
    if (!PACK_Load())
        return false;
    g_demo = true;              /* this boot installed the fallback; later boots load it normally */
    return true;
}

/* ---- mutations: the single choke point (spec §8) ----
 * sealed (RACE)   → refused, returns false
 * PRACTICE mode   → RAM-only (visible this session, dropped on power-off)
 * otherwise       → persisted: minimal EEPROM region, header CRC rewritten */

bool PACK_SaveLockout(uint8_t car_index, bool locked)
{
    if (car_index >= PACK_CarCount())
        return false;
    if (g_sealed)
        return false;                          /* "SEALED" status */
    if (locked)
        g_lockout[car_index / 8] |= (uint8_t)(1u << (car_index % 8));
    else
        g_lockout[car_index / 8] &= (uint8_t)~(1u << (car_index % 8));
    if (!g_practice)
        header_commit();                       /* bitmap + CRC; unchanged chunks are no-ops */
    return true;
}

bool PACK_SaveFavorite(uint8_t car_index, bool favorite)
{
    if (car_index >= PACK_CarCount())
        return false;
    if (g_sealed)
        return false;
    g_cars[car_index].favorite = favorite;
    if (!g_practice)
        car_meta_write(car_index, &g_cars[car_index]);   /* CarMeta is outside the CRC */
    return true;
}

bool PACK_SetMyDriver(const char *number)
{
    int i;
    if (g_sealed)
        return false;
    memset(g_my_driver, 0, 4);
    if (number != NULL) {
        for (i = 0; i < 4 && number[i] != 0; i++)
            g_my_driver[i] = number[i];
    }
    if (!g_practice)
        header_commit();
    return true;
}

bool PACK_AddCapture(const PackCar_t *entry)
{
    uint8_t n;
    bool duplicate = false;

    if (entry == NULL || g_sealed)
        return false;
    if (!PACK_FreqAllowed(entry->freq_hz,
                         g_practice ? PACK_MODE_PRACTICE : PACK_MODE_RACE))
        return false;                          /* band-lock: never stored out-of-band (ECPA) */
    if (PACK_CarCount() >= PACK_MAX_CARS)
        return false;                          /* PACK FULL — dump & trim */

    for (n = 0; n < PACK_CarCount(); n++) {
        if (strcmp(g_cars[n].number, entry->number) == 0) {
            duplicate = true;
            break;
        }
    }

    n = PACK_CarCount();
    g_cars[n] = *entry;
    g_cars[n].channel = n;              /* cars always at ch 0..63 (spec §5.1) */
    g_cars[n].origin = PACK_ORIGIN_CAPTURED;
    g_cars[n].verified = false;
    if (duplicate) {
        /* "ALT " + the entry name truncated to 7 — never overwrite (§5.3) */
        strcpy(g_cars[n].name, "ALT ");
        strncpy(g_cars[n].name + 4, entry->name, 7);
        g_cars[n].name[10] = 0;
        while (g_cars[n].name[0] && g_cars[n].name[9] == ' ')
            g_cars[n].name[9] = 0;          /* no trailing space from truncation */
    }
    g_car_count = (uint8_t)(n + 1);

    if (g_practice)
        return true;                           /* RAM-only; dropped on power-off */

    bool am = g_cars[n].freq_hz >= 108000000u && g_cars[n].freq_hz < 137000000u;
    channel_record_write(n, g_cars[n].freq_hz, g_cars[n].tone_index,
                         g_cars[n].code_type, g_cars[n].narrow, am);
    name_team_write(n, g_cars[n].name, g_cars[n].team);
    car_meta_write(n, &g_cars[n]);
    header_commit();
    return true;
}

/* ---- accessors ---- */

bool PACK_IsValid(void)     { return g_valid; }
bool PACK_IsDemo(void)      { return g_valid && g_demo; }
bool PACK_IsSealed(void)    { return g_sealed; }
void PACK_SetPractice(bool practice)
{
    if (g_practice && !practice) {
        /* exiting PRACTICE discards the ephemeral session: reload from
         * EEPROM so RAM-only mutations vanish (spec §8). On load failure
         * keep the session alive rather than strand the radio. */
        if (PACK_Load())      /* success path already cleared g_practice */
            return;
        return;
    }
    g_practice = practice;
}
bool PACK_IsPractice(void)  { return g_practice; }

bool PACK_SetSealed(bool sealed)
{
    if (g_sealed == sealed)
        return true;
    g_sealed = sealed;
    if (!g_practice)
        header_commit();                    /* the flags byte is inside the CRC */
    return true;
}

bool PACK_RenameCar(uint8_t car_index, const char *name)
{
    if (!g_valid || car_index >= PACK_CarCount() || name == NULL)
        return false;
    if (g_sealed)
        return false;                        /* "SEALED" status — pack data is safe */
    strncpy(g_cars[car_index].name, name, 10);
    g_cars[car_index].name[10] = 0;
    if (!g_practice)
        name_team_write(car_index, g_cars[car_index].name, g_cars[car_index].team);
    return true;
}

uint8_t PACK_CarCount(void)     { return g_car_count; }
uint8_t PACK_StationCount(void) { return g_station_count; }

const PackCar_t *PACK_GetCar(uint8_t index)
{
    return (g_valid && index < PACK_CarCount()) ? &g_cars[index] : NULL;
}

const PackStation_t *PACK_GetStation(uint8_t index)
{
    return (g_valid && index < PACK_StationCount()) ? &g_stations[index] : NULL;
}

const uint8_t *PACK_LockoutBitmap(void) { return g_lockout; }
const char *PACK_Series(void)  { return g_series; }
const char *PACK_Track(void)   { return g_track; }
const char *PACK_Session(void) { return g_session; }
const char *PACK_Venue2(void)  { return g_venue2; }
const char *PACK_MyDriver(void){ return g_my_driver; }
