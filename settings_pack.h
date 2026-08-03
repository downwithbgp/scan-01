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
 */

#ifndef SETTINGS_PACK_H
#define SETTINGS_PACK_H

#include <stdbool.h>
#include <stdint.h>

#define PACK_VERSION        0x01u
#define PACK_MAX_CARS       64
#define PACK_MAX_STATIONS   24

#define PACK_GROUP_A        0
#define PACK_GROUP_B        1
#define PACK_GROUP_C        2
#define PACK_GROUP_ALL      3

#define PACK_ORIGIN_PACK    0
#define PACK_ORIGIN_CAPTURED 1
#define PACK_ORIGIN_MANUAL  2

#define PACK_CT_NONE        0
#define PACK_CT_CTCSS       1   /* base CODE_TYPE_CONTINUOUS_TONE */
#define PACK_CT_DCS         2   /* base CODE_TYPE_DIGITAL */

#define PACK_KIND_BROADCAST 0   /* BK1080 — no channel record */
#define PACK_KIND_CONTROL   1
#define PACK_KIND_PA        2
#define PACK_KIND_OFFICIALS 3
#define PACK_KIND_SAFETY    4
#define PACK_KIND_RADIO     5
#define PACK_KIND_MEDIA     6
#define PACK_KIND_OTHER     7

typedef struct {
    char     number[4];      /* 1–3 chars + NUL ("24", "29A") */
    uint8_t  group;          /* PACK_GROUP_* */
    bool     favorite;
    uint8_t  origin;         /* PACK_ORIGIN_* */
    bool     verified;
    uint8_t  venue;          /* 0 = primary, 1 = secondary */
    uint16_t channel;        /* base memory-channel slot (16-byte record) */
    char     name[11];       /* "LAST INITIAL" (10 + NUL) */
    char     team[7];        /* team short name (6 + NUL) */
    uint32_t freq_hz;        /* RX frequency */
    uint8_t  tone_index;     /* CTCSS_Options / DCS_Options index */
    uint8_t  code_type;      /* PACK_CT_* */
    bool     narrow;         /* 12.5 kHz narrowband */
} PackCar_t;

typedef struct {
    char     name[5];        /* 4 chars + NUL */
    uint32_t freq_hz;
    uint8_t  tone_index;     /* table index; 0 = none */
    bool     tone_is_dcs;    /* tone_index is a DCS_Options index */
    uint8_t  kind;           /* PACK_KIND_* */
    bool     digital;        /* scan-skip; listed only */
    uint8_t  venue;
    uint16_t channel;        /* base slot; 0xFFFF for kind BROADCAST */
} PackStation_t;

/* Lifecycle — called once at boot (after SETTINGS_InitEEPROM). */
bool PACK_Init(void);                    /* load, or install+load the demo pack */

/* Load state */
bool     PACK_IsValid(void);             /* a valid pack is loaded (possibly demo) */
bool     PACK_IsDemo(void);              /* the flash-resident demo pack is active */
bool     PACK_IsSealed(void);
void     PACK_SetPractice(bool practice);
bool     PACK_IsPractice(void);
bool     PACK_SetSealed(bool sealed);
uint8_t  PACK_GetLessons(void);             /* bits 1-6 of the header flags, 1 = learned */
void     PACK_SetLessons(uint8_t lessons);  /* persists via the header CRC */
bool     PACK_RenameCar(uint8_t car_index, const char *name);

/* Accessors (consumed by the UI and the scan engine) */
uint8_t           PACK_CarCount(void);
uint8_t           PACK_StationCount(void);
const PackCar_t  *PACK_GetCar(uint8_t index);
const PackStation_t *PACK_GetStation(uint8_t index);
const uint8_t    *PACK_LockoutBitmap(void);
const char       *PACK_Series(void);
const char       *PACK_Track(void);
const char       *PACK_Session(void);
const char       *PACK_Venue2(void);
const char       *PACK_MyDriver(void);

/* Mutations — the single choke point (spec §8):
 *   sealed (RACE)  → refused, returns false ("SEALED" status)
 *   PRACTICE mode  → RAM-only, dropped on power-off/mode exit
 *   otherwise      → persisted (minimal EEPROM region + CRC rewrite) */
bool PACK_SaveLockout(uint8_t car_index, bool locked);
bool PACK_SaveFavorite(uint8_t car_index, bool favorite);
bool PACK_SetMyDriver(const char *number);
bool PACK_AddCapture(const PackCar_t *entry);   /* duplicate → "ALT " entry, never overwrite */

/* Pure helpers (host-tested) */
uint16_t PACK_Crc16Ccitt(const uint8_t *data, uint16_t len);

#endif
