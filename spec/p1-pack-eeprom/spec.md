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
- **The library is not in the radio:** the radio holds one weekend's subset (≤ 64 cars / ≤ 24 stations, §4.3); the full per-series/per-track library lives in `race-packs/library/` and is assembled by packtool `compose` (§7).
- **Never touch calibration**: EEPROM 0x1EC0–0x1FFF (RSSI cal, battery cal, VOX, build options) is read-only for packtool and the pack layer.
- **No VFO/frequency mode**: VFO slots (0x0C80–0x0D5F) stay factory; freq entry exists only as the CAPTURE path.
- **CHIRP coexistence**: channel records keep the base 16-byte format and 10-char names, so existing CHIRP/k5prog tooling still reads frequencies and names. RaceScan metadata lives in regions CHIRP never touches.

## 3. Pack JSON v0

```json
{
  "meta": { "series": "NASCAR Cup", "track": "Daytona", "session": "Race",
            "season": 2026, "date": "2026-02-15", "author": "n4racin",
            "version": 1, "venues": ["Daytona", "New Smyrna"] },
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
- `number`: 1–3 digits + optional one suffix letter (`"29A"`, `"13F"`) — **max 3 chars total** (the binary field is 3 bytes, §4.3). The string is the identity; `"29"` ≠ `"29A"`. No leading zeros.
- `freqs`: 1–2 entries, MHz decimal, 450.000–470.000 MHz (teams/control) or 162.400–162.550 (NOAA — `kind: noaa` stations only). Broadcast stations use `fm` (88.0–108.0) and have no `freqs`. Station `freq` may additionally be **151.000–160.000 MHz** (VHF dirt-track PA/operations — WoO track guidance), **108.000–137.000 MHz** (airband — requires `"modulation": "am"`), **156.000–162.000 MHz** (marine VHF), and **462.550–467.725 MHz** (FRS/GMRS) — the daily-use bands (§1.5 of the vision).
- `modulation`: default `"fm"`; `"am"` for airband entries (base enum: `MODULATION_FM=0`, `MODULATION_AM=1` — byte 11, §4.1). AM is only valid on 108–137 MHz.
- `tone`: `0` = none · CTCSS in Hz (`94.8`) · **DCS as a zero-padded 3-digit octal string (`"271"`, `"032"`)** — IndyCar mandates a unique DCS/DPL code per car (rulebook 7.4.1.3), so DCS is first-class. Values must exist in the base tables (`CTCSS_Options[50]`, `DCS_Options[104]` — both in dcs.c); packtool maps value → index.
- `bandwidth`: `"narrow"` (default, 12.5 kHz — IndyCar rulebook 7.4.1.1) or `"wide"`.
- `group`: `"A" | "B" | "C" | "ALL"` (scan groups). `favorite`: bool. `origin`: `"pack" | "captured" | "manual"`. `verified`: bool.
- `venue`: entry index into `meta.venues` (default 0). `meta.venues` names the events in a multi-venue weekend (Brickyard + IRP); `track` is `venues[0]`. **v0 supports exactly two venues** — 3+ fold into venue 1 with a compose warning.
- Stations: `kind` is `"broadcast" | "control" | "pa" | "officials" | "safety" | "radio" | "media" | "other"`; `"radio"` covers network feeds on UHF (MRN/PRN — e.g. MRN is 454.2 MHz at IMS, not commercial FM). `"digital": true` flags a feed RaceScan v0 cannot decode (it is listed but skipped by the scan).
- `lockouts`: array of car numbers currently locked out of the scan.
- Cap: **≤ 64 cars** (alternate frequencies count as entries, see §5.1) and **≤ 24 stations** (the IMS weekend alone needs 17: PA×3, media×3, radio×4, control×2, safety×2, officials×3).

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
| **Pack table** | **0x1BD0** | ≤ 556 | §4.3 — header + CarMeta + StationMeta |
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
| +8 | 1 | RX code | **index into `CTCSS_Options[50]` (CT=1) or `DCS_Options[104]` (CT=2)** — both tables in dcs.c; 0 = none. packtool maps Hz/octal → index |
| +9 | 1 | TX code | same as RX (harmless; TX compiled out) |
| +10 | 1 | (TX code type << 4) \| RX code type | `0x11` CTCSS · `0x22` DCS (`CODE_TYPE_DIGITAL=2`) · `0x00` none — without the type the tone is inert (radio.c gates on it) |
| +11 | 1 | (modulation << 4) \| offset direction | FM (0) for cars/stations by default; AM (1) for airband entries — from `modulation`; offset off |
| +12 | 1 | TX_LOCK<<6 \| BUSY_LOCK<<5 \| POWER<<2 \| BW<<1 \| FREV | `0x46` = TX_LOCK(1) + power low(1) + **narrow bandwidth(1)** — 12.5 kHz narrowband is mandated by IndyCar rulebook 7.4.1.1; `bandwidth: "wide"` entries clear the BW bit |
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
0x0D  12    track — primary venue name, ASCII space-padded ("DAYTONA    ")
0x19  8     session, ASCII space-padded ("RACE    ", "QUALI   ", "PRACT   ")
0x21  1     car count (1–64)
0x22  1     station count (0–24)
0x23  8     lockout bitmap (bit i = car slot i locked out)
0x2B  4     my-driver number, ASCII NUL-padded ("24\0\0", "29A\0")
0x2F  8     venue2 — secondary venue name, ASCII space-padded (≤ 8 chars)
0x37  2     CRC16-CCITT over bytes 0x00..0x36
0x39  —     pad to 0x3C
0x3C  n×4   CarMeta[car], n = car count (≤ 64)
0x3C+4n  m×10  StationMeta[station], m = station count (≤ 24)
```

**CarMeta (4 B):** `number[3]` ASCII NUL-padded ("24\0", "29A", "100") + `flags[1]`:

| bits | field | values |
|---|---|---|
| 0–1 | group | 0=A, 1=B, 2=C, 3=ALL |
| 2 | favorite | 0/1 |
| 3–4 | origin | 0=pack, 1=captured, 2=manual |
| 5 | verified | 0/1 |
| 6 | venue | 0=primary (header track), 1=secondary (header venue2) |
| 7 | reserved | 0 |

**StationMeta (10 B):** `name[4]` ASCII NUL-padded ("MRN\0", "CTRL", "PA\0\0") + `freq[4]` **u32 LE Hz** (broadcast: 101.1 MHz → 101100000; non-broadcast: 461200000) + `tone[1]` (bit 7 = DCS flag; bits 0–6 = table index; 0x00 = none) + `kind[1]` (bits 0–2 = kind, bit 3 = digital — digital stations are listed but **skipped by the scan**, bit 4 = venue 0/1):

| kind | meaning | RX path | needs channel record |
|---|---|---|---|
| 0 | broadcast (commercial FM — MRN/PRN/IMSA Radio when on FM) | BK1080 | no |
| 1 | race control | BK4819 | yes |
| 2 | track PA | BK4819 | yes |
| 3 | officials | BK4819 | yes |
| 4 | safety | BK4819 | yes |
| 5 | radio network (MRN/PRN on UHF, e.g. MRN 454.2 at IMS) | BK4819 | yes |
| 6 | media (NBC crew etc.) | BK4819 | yes |
| 7 | other | BK4819 | yes |

Sizes: header 0x3C (60) + 64×4 (256) + 24×10 (240) = **556 B ≤ 560 B** available. 4 B spare.

The budget is **`60 + 4·n + 10·m ≤ 560`** (n cars, m stations): 64 cars → 24 stations, 36 cars → 35 stations, 26 cars → 39, 0 cars → 50. Multi-event weekends (Brickyard + IRP, Daytona 24 + short track) exceed the caps by design — packtool `compose` reports the trade-off and lets the user trim; v1 can rebalance the caps without a format change.

**Wear note:** lockout toggles rewrite header bytes 0x23–0x38 (22 bytes, spanning four 8-byte chunks — byte 0x38 falls in its own page). EEPROM endurance (~1M writes) makes this a non-issue at race-day rates; noted for the record.

### 4.4 Factory reset (edition change required)

The base's `SETTINGS_FactoryReset` behavior around the pack table is wrong for us:
- **Partial reset** (`bIsAll=false`): preserves names (0x0F50–0x1C00) but wipes channel records (0x0000–0x0C7F) → a header-valid pack whose channels are 0xFF.
- **Full reset** (`bIsAll=true`): wipes 0x0000–0x1DFF except DTMF/AES/welcome/voice regions → pack header + names erased, but CarMeta/StationMeta at 0x1C00+ survive as orphans.

**The RaceScan edition must extend both paths to wipe 0x1BD0–0x1DFF** (magic gone → clean "NO PACK" state). Additionally, `PACK_Load` sanity-checks channel records (frequency within RX bands) so a stale header can never resurrect a broken pack.

## 5. Mapping rules

### 5.1 Build (pack → EEPROM)

1. Validate (§6). On error: abort with a report; on warning: proceed and print.
2. **Flatten cars:** each car with `freqs[1]` produces a second entry `number` (same) + `name = "ALT <last>"` + same tone/group/flags. Array order is preserved; `n = cars + alts ≤ 64`.
3. **Assign channels:** cars/alts → ch 0..n−1; non-broadcast stations → ch n..n+m−1 (broadcast stations get no channel record). `n ≤ 64`, `m ≤ 24`, `n + m ≤ 88 ≤ 200`. **Entries are ordered by venue — all venue-0 entries, then all venue-1 — so LIST dividers render once each.**
4. Write channel records (§4.1 — incl. per-entry bandwidth), names + team (§4.2), pack table (§4.3) with CRC over the header.
5. Channels beyond `n+m` in 0..79 are zeroed (so a stale pack never half-survives); 80..199 left as-is.
6. Writes are region-scoped (see §7) — calibration and the settings block are never written by `build`.

### 5.2 Dump (EEPROM → pack)

Reverse of §5.1: read pack table (validate magic + CRC; if invalid, report "radio has no valid pack"), read names/team, reconstruct cars (alts become `freqs[1]` — matched by number + `ALT ` name prefix, best-effort), read lockout bitmap + my-driver, emit JSON. Venues are reconstructed from the header's `track` (venue 0) and `venue2` (venue 1); the entry venue bit maps back to `meta.venues` indices. `origin`/`verified` round-trip through CarMeta flags. Captured entries come out `origin: "captured", verified: false` — ready for the community pipeline.

### 5.3 Capture lifecycle (on-radio)

- New capture (vision §4.5): write a fresh channel record + name (`ALT <last>` or `NEW` + number) + CarMeta with `origin=1 (captured), verified=0`, update counts + CRC.
- Duplicate number → append as `ALT` entry (never overwrite, vision §4.5).
- **Pack full (64 cars)**: CAPTURE save is rejected with a "PACK FULL — dump & trim" status (screen + no write). Capture still works; only persistence is refused.
- Lockout toggle → flip bitmap bit, rewrite header + CRC (persists across reboots — the base keeps lockout in RAM only; this is our fix).
- Favorite toggle / my-driver change → CarMeta flag / header field, rewrite CRC.

## 6. Validation (packtool `validate`)

Hard errors:
- number format (1–3 digits + optional letter), duplicates (after flattening; `ALT` entries exempt via `freqs[1]`), leading zeros.
- frequency outside RX bands: cars 450.000–470.000; stations 450.000–470.000 / 162.400–162.550 / **151.000–160.000 (VHF dirt-track)** / **108.000–137.000 (airband, AM only)** / **156.000–162.000 (marine)** / **462.550–467.725 (FRS/GMRS)**; broadcast 88.0–108.0.
- **cellular band-lock (ECPA): 824–894, 1710–1755, 1850–1990, 2110–2155 MHz → hard reject, always.**
- counts > 64 cars / 24 stations; name > 10 chars or team > 6 after composition (unless auto-truncated with warning); tone not in `CTCSS_Options` / `DCS_Options`; station name > 4 chars.

Warnings: duplicate frequency across entries (IndyCar rulebook 7.4.1.2 requires unique per-car freqs — flag, don't fail), number collides with a station name, digital station (not monitorable by RaceScan v0 — the scan skips it), missing driver/team fields, venue index ≥ 2 (folded into venue 1 by compose), venue-2 name > 8 chars (truncated in the binary), unverified pack (`verified: false` on the pack meta is fine — only entries carry the flag).

## 7. packtool v0 contract

Python CLI (stdlib + `pyserial`), no firmware knowledge needed by users:

| Command | Behavior |
|---|---|
| `packtool validate pack.json` | §6 checks; exit code 0/1/2 (ok/warnings/errors) |
| `packtool import <file> <format>` | parse a published frequency list into pack.json (first parser: `indyspeedway` text format; fixture: `race-packs/indyspeedway-ims-2026/source.txt`, expected output: the checked-in draft `pack.json`) |
| `packtool compose events.json` | assemble a weekend pack: load event packs + library series stations (`race-packs/library/series/`), merge + dedupe by frequency, **assign venues per event (≤ 2; `events.json` maps each event to `venues[0]`/`venues[1]`)**, report the capacity trade-off (§4.3) when caps are exceeded with trim options |
| `packtool library list [series\|track]` | list library stations matching a series/track (drives the desktop app's picker) |
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

- `PACK_Load()` at boot: read header, validate magic + CRC, populate RAM arrays (`PackCar[64]`, `PackStation[24]`, lockout bitmap, my-driver). Failure → **demo-pack fallback** (a flash-resident daily pack: airband guard/unicom, marine 16/9, FRS 1/7/14 — from `race-packs/library/daily/daily-presets.json`, compiled in) so the radio always boots into something useful. "NO PACK" remains only as a SETUP diagnostic; capture still works either way.
- `PACK_SaveLockout(i)`, `PACK_SaveFavorite(i)`, `PACK_SetMyDriver()`, `PACK_AddCapture(entry)` — each writes the minimal region + CRC.
- Boot screen (vision §5.8) renders series/track/session + car count from the header.
- Band-lock: a single filter function `PACK_FreqAllowed(freq)` used by every tune path (BK4819 set, CAPTURE, BRD, WX) — single point of enforcement.

## 9. Open questions (P0 validation items)

1. Name composition (`"BYRON W"` / `"ALT BYRON"`) — does the initial help or confuse at the track? (Vision says the first name lives in LIST; v0 device shows initial only.)
2. 64-car cap vs. IMSA's 50+ entries with alts — is 64 enough per weekend, or should v1 raise it (channels 80–199 are free; the cap is pack-table size)?
3. Team truncation to 6 chars — acceptable, or should the SCAN team line go away entirely?
4. ~~Is CTCSS-only a real gap?~~ **Resolved by P0 data (indyspeedway-ims-2026):** IndyCar mandates DCS/DPL unique per car (rulebook 7.4.1.3) — DCS is first-class in the spec. Follow-up: digital race-control feeds (e.g. IMS Safety 2 on 461.4250) — accept as flagged-and-skipped in v0?
5. CHIRP coexistence priority: do we *need* CHIRP to write packs, or is packtool the only writer (CHIRP remains read-only-compatible)?

## 10. What this spec deliberately does NOT decide

- Scan engine parameters (P2), FOLLOW mode, adaptive squelch.
- The 32 px racing-digits font bitmaps (P1 task 4, vision §5.6).
- BRD/WX screen details (vision §5.10 — implemented per that spec).
- Hardware v2 storage (P3 — same JSON, different binary).
