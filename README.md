Scan 01
--------

Scan 01 is a receive-only scanner firmware for the Quansheng UV-K5/K6
(and family) handheld radios, built for race fans. It turns a $25 radio
into a race scanner: car numbers instead of frequencies, a weekend
"pack" of channels loaded from a PC, and an interface that starts
scanning the moment it powers on. It is a specialized edition of the
F4HWN v4.3 firmware (a fork of egzumer's UV-K5 firmware), vendored at
the root of this repository.

The firmware is in active development. There are no releases yet; see
[Project status](#project-status) for what works today.

### Why Scan 01

Racing Electronics owns the race-scanner market. A weekend rental runs
$50–80, a purchase $300–600, and the frequency guides are sold
separately, every season, per track. The radio this firmware runs on
already contains everything a race scanner needs: FM reception across
the racing bands, a numeric keypad, a 128×64 display, a knob. The
firmware is free and the frequency data is open.

The full design is in [docs/design/hci-vision.md](docs/design/hci-vision.md).

### What makes it different

* The car number is the identity. Channels are cars ("24", "29A"), not
  frequencies; typing a number jumps to that car.
* RACE and PRACTICE modes. RACE is the pack world: boot, scan, hold.
  PRACTICE is an ephemeral sandbox for the airport, the car ride, and
  the kids — every change evaporates on power-off, and a sealed pack
  cannot be modified by button-mashing.
* No menus in the hot path. Seven rules total (vision doc §3). Long-press
  EXIT is HOME: back to the car browser from anywhere.
* Receive-only, always. TX is locked at the channel-record level, and US
  cellular bands are hard-rejected in firmware (ECPA §2511).
* Packs instead of programming. A weekend's listening world is a JSON
  pack, composed by a PC tool and flashed over USB: cars, stations (race
  control, PA, MRN, weather), lockouts, favorites.
* Open data. [`race-packs/`](race-packs/) holds event packs and a
  frequency library — the thing the incumbents sell is the thing we open.

### Hardware

Quansheng UV-K5 / UV-K6 and family: BK4819 receiver, 128×64 LCD, 16-key
keypad, two side keys, rotary encoder. The firmware has a 60 KB flash
budget and 16 KB of RAM.

### Project status

| Task | State |
| ---- | ----- |
| T1 — edition scaffold | Done — `scan01.packed.bin` builds, 3.8 KB flash headroom |
| T2 — band-lock (`PACK_FreqAllowed`) | Done — 815,004 host checks, 0 failures |
| T3–T10 — pack layer, screens, capture, packtool | Pending |

The task list is [spec/p1-pack-eeprom/tasks.md](spec/p1-pack-eeprom/tasks.md);
the scan-engine spec is [spec/p2-scan-engine/](spec/p2-scan-engine/).

### Building

Docker is the supported toolchain path (no ARM toolchain needed on the
host):

    $ docker build --build-arg ALPINE_TAG=3.22 -t uvk5 .
    $ ./compile-with-docker.sh scan01

or directly:

    $ docker run -v "$PWD:/app" uvk5 /bin/bash /app/build-scan01.sh

Output: `scan01.packed.bin` (plus `scan01`, `scan01.bin`). The build is
gated: text must stay within 59.5 KB (60 KB minus a 512 B no-brick floor).
CI enforces this on every push. Flash budget: the pack layer (T3) ~3.1 KB,
the racing font ~2.4 KB, the key layer ~1.1 KB, the Scan 01 UI (screens,
SETUP, editor, BRD/WX) ~8 KB; the stock ham menu stack (~9.3 KB) and the
stock main-screen island (~7.2 KB) were removed when the Scan 01 UI
landed (T6a/T6b).

### Running the tests

Host tests (band-lock, pack layer):

    $ gcc -Wall -Werror -Wextra -I. tests/test_bandlock.c pack_bandlock.c -o /tmp/test_bandlock
    $ /tmp/test_bandlock
    $ gcc -Wall -Werror -Wextra -I. tests/test_pack.c settings_pack.c pack_bandlock.c -o /tmp/test_pack
    $ /tmp/test_pack
    $ gcc -Wall -Werror -Wextra -I. tests/test_font.c font_racing.c font_racing_data.c -o /tmp/test_font
    $ /tmp/test_font
    $ gcc -Wall -Werror -Wextra -I. tests/test_keys.c scan01_keys.c -o /tmp/test_keys
    $ /tmp/test_keys
    $ gcc -Wall -Werror -Wextra -I. tests/test_edit.c scan01_edit.c -o /tmp/test_edit
    $ /tmp/test_edit

CI runs these on every push, then builds the firmware and checks the
flash gate.

### Why shouldn't I use Scan 01 yet?

* There are no releases and no stable binary. Flashing today means
  building from source.
* The scan engine, the pack layer, and packtool are not implemented.
  The radio currently runs the base F4HWN UI, not the Scan 01 interface.
* The receiver is a $25 wide-open front end. In a packed grandstand it
  will hear everything, including things you don't want. FM works; a
  tuned front end waits for the hardware v2 (vision doc §10).
* 2m ham (144–148 MHz) tuning is PRACTICE-mode only.

### Data

[`race-packs/`](race-packs/) contains event packs and the frequency
library. The IndyCar/IMS 2026 draft pack (26 cars, 17 stations) is
transcribed from the public indyspeedway.com page and is not yet
field-verified; see
`race-packs/indyspeedway-ims-2026/README.md` for provenance. Data
follows the sourcing rule (vision §6.2): public sources and own
observation only, never scraped from paid guides.

### Contributing

PRs are welcome. Two rules:

* **Tone:** competitors are named only in factual comparisons; market
  claims are sourced. No competitor bashing in commits, docs, or code.
* **Deletion test** (rams-audit): a feature must serve the seven rules,
  need no new key or setting the fan sees, not be doable in the app, and
  be usable during a caution flag. If it fails any, it goes to the app or
  the backlog.

### License

Apache-2.0, inherited from the vendored base (DualTachyon → OneOfEleven →
egzumer → F4HWN). See [LICENSE](LICENSE) and [NOTICE](NOTICE).

### Documentation

* [Design vision](docs/design/hci-vision.md) — the HCI reimagination
* [P1 spec — pack format and EEPROM layout](spec/p1-pack-eeprom/spec.md)
* [P2 spec — scan engine](spec/p2-scan-engine/spec.md)
* [Rams audit](docs/design/rams-audit.md) — self-assessment against the
  ten principles
