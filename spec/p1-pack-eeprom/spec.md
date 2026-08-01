# P1 Spec — Pack model & EEPROM layout (RaceScan edition)

**Status:** v0.1 — spec for review
**Applies to:** the RaceScan edition of the F4HWN v4.3 base (`F4HWN/src/uv-k5-firmware-custom-4.3/`)
**Sources verified:** `settings.h` (EEPROM_Config_t, VFO_Info_t), `settings.c` (SETTINGS_SaveChannel, region map), `driver/eeprom.c` (8 KB @ 0xA0), `misc.h` (ChannelAttributes_t, MR_CHANNEL_LAST=199), `app/aircopy.c` (0x1E00 boundary)
**Related:** `docs/design/hci-vision.md` (§4.5 capture, §5 screens, §6 pack, §7 RX-only)

---

## 1. Scope

Locks down the **weekend pack** end-to-end:

1. **Pack JSON v0** — the authoring format (what humans, packtool, and the community `race-packs/` repo write).
2. **EEPROM binary layout** — how a pack lives in the radio's 8 KB EEPROM, byte by byte, reusing the base's channel records and using only regions the RaceScan edition owns.
3. **Mapping rules** — pack → EEPROM (build) and EEPROM → pack (dump), including truncation, validation, and the capture lifecycle (`origin`/`verified`).
4. **packtool v0 contract** — CLI commands and the UART region-write protocol.

Out of scope (later phases): scan-engine tuning (dwell/tone-lock/FOLLOW — P2), adaptive squelch, hardware v2, CHIRP-driver changes.

## 2. Non-goals & hard constraints

- **RX-only, always** (vision §7): channel records are written with TX locked; TX paths compiled out; band-lock table rejects cellular ranges (ECPA §2511).
- **Never touch calibration**: EEPROM 0x1EC0–0x1FFF (RSSI cal, battery cal, VOX, build options) is read-only for packtool and the pack layer.
- **No VFO/frequency mode**: VFO slots (0x0C80–0x0D5F) stay factory; freq entry exists only as the CAPTURE path.
- **CHIRP coexistence**: channel records keep the base 16-byte format and 10-char names, so existing CHIRP/k5prog tooling still reads frequencies and names. RaceScan metadata lives in regions CHIRP never touches.

## 3. Pack JSON v0

```json
{
  "meta": { "series": "NASCAR Cup", "track": "Daytona", "session": "Race",
            "season": 2026, "date": "2026-02-15", "author": "n4racin",
            "version": 1 },
  "cars": [
    { "number": "24", "driver": "William Byron", "team": "Hendrick Motorsports",
      "entry": "HMS · CHEVY", "freqs": [450.8875, 451.1125],
      "tone": 94.8, "group": "A", "favorite": true,
      "origin": "pack", "verified": true },
    { "number": "29A", "driver": "...", "team": "...", "freqs": [451.1250],
      "tone": 0, "group": "B", "favorite": false,
      "origin": "captured", "verified": false }
  ],
  "stations": [
    { "name": "MRN",   "kind": "broadcast", "fm": 101.1 },
    { "name": "PA",    "kind": "pa",        "freq": 464.500 },
    { "name": "CTRL",  "kind": "control",   "freq": 461.200, "tone": 0 },
    { "name": "WX",    "kind": "noaa" }
  ],
  "lockouts": ["29", "13F"]
}
```

**Rules:**
- `number`: 1–3 digits + optional one suffix letter (`"29A"`, `"13F"`). The string is the identity; `"29"` ≠ `"29A"`. No leading zeros.
- `freqs`: 1–2 entries, MHz decimal, 450.000–470.000 MHz (teams/control) or 162.400–162.550 (NOAA — `kind: noaa` stations only). Broadcast stations use `fm` (88.0–108.0) and have no `freqs`.
- `tone`: 0 = none, else CTCSS in Hz (67.0–254.1). DCS is rejected with a warning in v0.
- `group`: `"A" | "B" | "C" | "ALL"` (scan groups). `favorite`: bool. `origin`: `"pack" | "captured" | "manual"`. `verified`: bool.
- `lockouts`: array of car numbers currently locked out of the scan.
- Cap: **≤ 64 cars** (alternate frequencies count as entries, see §5.1) and **≤ 16 stations** (broadcast + non-broadcast).

## 4. EEPROM layout (RaceScan edition)

Total 8 KB (0x0000–0x1FFF). The pack owns four regions; everything else is either base-owned or factory.

| Region | Address | Size | Owner |
|---|---|---|---|
| Channel records (ch 0–199) | 0x0000 | 3200 | base 16-byte format (§4.1); RaceScan uses ch 0..(used-1) |
| VFO slots | 0x0C80 | 224 | untouched (compiled out) |
| Channel attributes | 0x0D60 | 207 | untouched (zero; scan lists compiled out) |
| Settings block | 0x0E28–0x0F4F | ~296 | base gEeprom fields (subset used) |
| FM broadcast channels | 0x0E40 | — | untouched (BRD presets come from StationMeta) |
| **Channel names + team** | **0x0F50** | 200×16 | §4.2 — name (10 B, base-compatible) + team (6 B, RaceScan-only) |
| **Pack table** | **0x1BD0** | ≤ 532 | §4.3 — header + CarMeta + StationMeta |
| DTMF contacts | 0x1C00 | 512 | compiled out in RaceScan; part of pack-table space |
| Aircopy | 0x1E00 | — | compiled out; keep clear |
| Calibration / options | 0x1EC0–0x1FFF | 320 | **never written by packtool or the pack layer** |

The pack table fits in 0x1BD0–0x1DFF (560 B continuous): names end at 0x1BD0 (0x0F50 + 200×16). **0x1DFF is a hard boundary** — the base's squelch calibration tables live at 0x1E00/0x1E60 (`RADIO_ConfigureSquelchAndOutputPower`, radio.c) and aircopy uses 0x1E00 as a marker. Nothing reads 0x1BD0–0x1DFF at runtime (verified by grep).

### 4.1 Channel record (16 bytes, base format — `SETTINGS_SaveChannel`)

RaceScan writes RX-only-safe values:

| Off | Size | Field | RaceScan value |
|---|---|---|---|
| +0 | 4 | RX frequency | **u32 LE Hz** (450.8875 MHz → `450887500` = 0x1AE3A94C) — the base stores plain Hz (`SETTINGS_SaveChannel` writes `freq_config_RX.Frequency` raw; cf. `RADIO_InitInfo(..., 43350000)`) |
| +4 | 4 | TX offset | 0 |
| +8 | 1 | RX code | **index into `CTCSS_Options[]`** (base table, deci-Hz values; 0 = none). packtool maps Hz → index |
| +9 | 1 | TX code | same as RX (harmless; TX compiled out) |
| +10 | 1 | (TX code type << 4) \| RX code type | `0x11` when a tone is set, else `0x00` (`CODE_TYPE_CONTINUOUS_TONE = 1`, dcs.h) — without this the tone is inert (`BK4819_SetCTCSSFrequency` is gated on it, radio.c) |
| +11 | 1 | (modulation << 4) \| offset direction | 0 (FM, offset off) |
| +12 | 1 | TX_LOCK<<6 \| BUSY_LOCK<<5 \| POWER<<2 \| BW<<1 \| FREV | `0x44` = TX_LOCK(1) + power low(1) |
| +13 | 1 | DTMF PTT ID / decode | 0 |
| +14 | 1 | STEP_SETTING | 0 |
| +15 | 1 | scrambling (F4HWN: 0) | 0 |

> Note: base code-type enum lives in `dcs.h` (`CODE_TYPE_OFF=0, CODE_TYPE_CONTINUOUS_TONE=1, CODE_TYPE_DIGITAL=2`); the implementer copies the base's exact packing in `SETTINGS_SaveChannel` — the spec fixes the *values*, not a reimplementation.

### 4.2 Channel name + team (16 B per channel at 0x0F50 + ch×16)

- **Name (bytes 0–9, base-compatible 10 chars):** uppercase ASCII, composed by packtool as `LASTNAME <FIRST-INITIAL>` when a first name exists (`"BYRON W"`, `"TRUEX JR M"`), else last name only. Space-padded, no NUL needed (base trims on read; RaceScan also trims). Alts use `ALT <LASTNAME>` (`"ALT BYRON"`).
- **Team (bytes 10–15, RaceScan-only):** uppercase team short name ≤ 6 chars (`"HMS"`, `"PENSKE"`), space-padded. CHIRP/k5prog never see these bytes; the base never reads them.
- The SCAN screen shows the full 10-char name at 16 px (the initial is part of the name — no parsing). The team line shows the 6-char team.

### 4.3 Pack table (0x1BD0)

```
off  size  field
0x00  4     magic "RSPK" (0x52 0x53 0x50 0x4B)
0x04  1     format version = 0x01
0x05  8     series, ASCII space-padded ("NASCARCUP", "IMSA    ", "INDYCAR ")
0x0D  12    track, ASCII space-padded ("DAYTONA    ")
0x19  8     session, ASCII space-padded ("RACE    ", "QUALI   ", "PRACT   ")
0x21  1     car count (1–64)
0x22  1     station count (0–16)
0x23  8     lockout bitmap (bit i = car slot i locked out)
0x2B  4     my-driver number, ASCII NUL-padded ("24\0\0", "29A\0")
0x2F  2     CRC16-CCITT over bytes 0x00..0x2E
0x31  —     pad to 0x34
0x34  n×5   CarMeta[car], n = car count
0x34+5n  m×10  StationMeta[station], m = station count
```

**CarMeta (5 B):** `number[4]` ASCII NUL-padded ("24\0\0\0", "29A\0", "100\0") + `flags[1]`:

| bits | field | values |
|---|---|---|
| 0–1 | group | 0=A, 1=B, 2=C, 3=ALL |
| 2 | favorite | 0/1 |
| 3–4 | origin | 0=pack, 1=captured, 2=manual |
| 5 | verified | 0/1 |
| 6–7 | reserved | 0 |

**StationMeta (10 B):** `name[4]` ASCII NUL-padded ("MRN\0", "CTRL", "PA\0\0") + `freq[4]` **u32 LE Hz** (broadcast: 101.1 MHz → 101100000; non-broadcast: 461200000) + `tone[1]` (0 = none) + `kind[1]`:

| kind | meaning | RX path | needs channel record |
|---|---|---|---|
| 0 | broadcast (MRN/PRN/IMSA Radio) | BK1080 | no |
| 1 | race control | BK4819 | yes |
| 2 | track PA | BK4819 | yes |
| 3 | officials | BK4819 | yes |
| 4 | other | BK4819 | yes |

Sizes: header 0x34 (52) + 64×5 (320) + 16×10 (160) = **532 B ≤ 560 B** available. 28 B spare.

**Wear note:** lockout toggles rewrite header bytes 0x23–0x30 (two 8-byte pages). EEPROM endurance (~1M writes) makes this a non-issue at race-day rates; noted for the record.

### 4.4 Factory reset (edition change required)

The base's `SETTINGS_FactoryReset` behavior around the pack table is wrong for us:
- **Partial reset** (`bIsAll=false`): preserves names (0x0F50–0x1C00) but wipes channel records (0x0000–0x0C7F) → a header-valid pack whose channels are 0xFF.
- **Full reset** (`bIsAll=true`): wipes 0x0000–0x1DFF except DTMF/AES/welcome/voice regions → pack header + names erased, but CarMeta/StationMeta at 0x1C00+ survive as orphans.

**The RaceScan edition must extend both paths to wipe 0x1BD0–0x1DFF** (magic gone → clean "NO PACK" state). Additionally, `PACK_Load` sanity-checks channel records (frequency within RX bands) so a stale header can never resurrect a broken pack.

## 5. Mapping rules

### 5.1 Build (pack → EEPROM)

1. Validate (§6). On error: abort with a report; on warning: proceed and print.
2. **Flatten cars:** each car with `freqs[1]` produces a second entry `number` (same) + `name = "ALT <last>"` + same tone/group/flags. Array order is preserved; `n = cars + alts ≤ 64`.
3. **Assign channels:** cars/alts → ch 0..n−1; non-broadcast stations → ch n..n+m−1 (broadcast stations get no channel record). `n + m ≤ 80 ≤ 200`.
4. Write channel records (§4.1), names + team (§4.2), pack table (§4.3) with CRC over the header.
5. Channels beyond `n+m` in 0..79 are zeroed (so a stale pack never half-survives); 80..199 left as-is.
6. Writes are region-scoped (see §7) — calibration and the settings block are never written by `build`.

### 5.2 Dump (EEPROM → pack)

Reverse of §5.1: read pack table (validate magic + CRC; if invalid, report "radio has no valid pack"), read names/team, reconstruct cars (alts become `freqs[1]` — matched by number + `ALT ` name prefix, best-effort), read lockout bitmap + my-driver, emit JSON. `origin`/`verified` round-trip through CarMeta flags. Captured entries come out `origin: "captured", verified: false` — ready for the community pipeline.

### 5.3 Capture lifecycle (on-radio)

- New capture (vision §4.5): write a fresh channel record + name (`ALT <last>` or `NEW` + number) + CarMeta with `origin=1 (captured), verified=0`, update counts + CRC.
- Duplicate number → append as `ALT` entry (never overwrite, vision §4.5).
- **Pack full (64 cars)**: CAPTURE save is rejected with a "PACK FULL — dump & trim" status (screen + no write). Capture still works; only persistence is refused.
- Lockout toggle → flip bitmap bit, rewrite header + CRC (persists across reboots — the base keeps lockout in RAM only; this is our fix).
- Favorite toggle / my-driver change → CarMeta flag / header field, rewrite CRC.

## 6. Validation (packtool `validate`)

Hard errors:
- number format (1–3 digits + optional letter), duplicates (after flattening; `ALT` entries exempt via `freqs[1]`), leading zeros.
- frequency outside RX bands: 450.000–470.000 / 162.400–162.550 / 88.0–108.0 (broadcast only).
- **cellular band-lock (ECPA): 824–894, 1710–1755, 1850–1990, 2110–2155 MHz → hard reject, always.**
- counts > 64 cars / 16 stations; name > 10 chars or team > 6 after composition (unless auto-truncated with warning); tone outside CTCSS range; station name > 4 chars.

Warnings: DCS tone (v0 unsupported), number collides with a station name, missing driver/team fields, unverified pack (`verified: false` on the pack meta is fine — only entries carry the flag).

## 7. packtool v0 contract

Python CLI (stdlib + `pyserial`), no firmware knowledge needed by users:

| Command | Behavior |
|---|---|
| `packtool validate pack.json` | §6 checks; exit code 0/1/2 (ok/warnings/errors) |
| `packtool build pack.json` | emits `pack.patch` — ordered list of `(addr, bytes)` region writes (§5.1) + human-readable summary |
| `packtool flash <port> pack.json` | `build` then apply over UART, region-scoped |
| `packtool dump <port>` | §5.2 → `pack.json` (plus a full `eeprom.bin` backup, k5prog-style) |
| `packtool diff a.json b.json` | entry-level diff (for race-packs submissions) |

**UART protocol (region-scoped):** reuse the base's existing PC-UART EEPROM access (the protocol the CHIRP driver already speaks — k5prog-style commands). The firmware side exposes:
- `read_eeprom(addr, len)` — len ≤ 64, any addr
- `write_eeprom(addr, bytes)` — len ≤ 64, any addr; **writes are applied in 8-byte chunks** (the base's `EEPROM_WriteBuffer` granularity) — packtool must align and chunk accordingly
- `pack_status()` — magic/version/counts/CRC/band-lock state (debug + boot self-check)

packtool *never* writes outside the pack-owned regions (§4); `dump` reads everything for backup. Exact command framing is pinned in P1 task 8 against `driver/uart.c` + the CHIRP driver's protocol.

## 8. Firmware pack layer (settings_pack.c — new)

- `PACK_Load()` at boot: read header, validate magic + CRC, populate RAM arrays (`PackCar[64]`, `PackStation[16]`, lockout bitmap, my-driver). Failure → "NO PACK" boot state (capture still works).
- `PACK_SaveLockout(i)`, `PACK_SaveFavorite(i)`, `PACK_SetMyDriver()`, `PACK_AddCapture(entry)` — each writes the minimal region + CRC.
- Boot screen (vision §5.8) renders series/track/session + car count from the header.
- Band-lock: a single filter function `PACK_FreqAllowed(freq)` used by every tune path (BK4819 set, CAPTURE, BRD, WX) — single point of enforcement.

## 9. Open questions (P0 validation items)

1. Name composition (`"BYRON W"` / `"ALT BYRON"`) — does the initial help or confuse at the track? (Vision says the first name lives in LIST; v0 device shows initial only.)
2. 64-car cap vs. IMSA's 50+ entries with alts — is 64 enough per weekend, or should v1 raise it (channels 80–199 are free; the cap is pack-table size)?
3. Team truncation to 6 chars — acceptable, or should the SCAN team line go away entirely?
4. Is CTCSS-only (no DCS) a real gap for any series? (NASCAR/IMSA/IndyCar are CTCSS-heavy; DCS rare.)
5. CHIRP coexistence priority: do we *need* CHIRP to write packs, or is packtool the only writer (CHIRP remains read-only-compatible)?

## 10. What this spec deliberately does NOT decide

- Scan engine parameters (P2), FOLLOW mode, adaptive squelch.
- The 32 px racing-digits font bitmaps (P1 task 4, vision §5.6).
- BRD/WX screen details (vision §5.10 — implemented per that spec).
- Hardware v2 storage (P3 — same JSON, different binary).
