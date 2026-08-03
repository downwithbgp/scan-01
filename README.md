Scan 01
--------

Scan 01 is a receive-only scanner firmware for the Quansheng UV-K5/K6
(and family) handheld radios, built for race fans. It turns a $25 radio
into a race scanner: car numbers instead of frequencies, a weekend
"pack" of channels loaded from a PC, and an interface that starts
scanning the moment it powers on. It is a specialized edition of the
F4HWN v4.3 firmware (a fork of egzumer's UV-K5 firmware), vendored at
the root of this repository.

The firmware is in active development. There are no stable releases yet
(weekly test builds ship as the rolling user-testing release); see
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
| T3–T8 — pack layer, racing font, key layer, Scan 01 UI (BRD/WX/SETUP/editor), capture, packtool | Done |
| P2 — scan engine (tone-lock, FOLLOW, CSQ guard) + scan01 racing pack | Done — engine shipped; race-packs data awaits community verification |

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
    $ printf 'void _putchar(char c) { (void)c; }\n' > /tmp/putchar_stub.c
    $ gcc -Wall -Werror -Wextra -I. tests/test_pack_uart.c pack_uart.c settings_pack.c pack_bandlock.c external/printf/printf.c /tmp/putchar_stub.c -o /tmp/test_pack_uart
    $ /tmp/test_pack_uart
    $ ./tools/build-sim.sh      # the headless radio: pixel-budget assertions
    $ python3 tools/pbm2png.py  # screenshots/*.png + index.html
    $ gcc -Wall -Werror -Wextra -I. tests/test_scan.c scan01_scan.c settings_pack.c pack_bandlock.c -o /tmp/test_scan
    $ /tmp/test_scan            # the scan engine (2048 checks)
    $ gcc -Wall -Werror -Wextra -I. tests/test_lessons.c scan01_lessons.c settings_pack.c pack_bandlock.c -o /tmp/test_lessons
    $ /tmp/test_lessons         # the fading-legend lesson engine (31 checks)
    $ ./tools/build-golden.sh   # regenerate the packtool's golden fixture
    $ python3 -m pytest tests/test_packtool_*.py -q   # packtool (48 tests)

The packtool (packtool/) turns community frequency data into firmware
bytes: `python3 -m packtool.cli validate|build|diff|import|compose|dump|flash|overlay|card`.
The overlay prints the honest-face keypad sheet (`packtool overlay`);
`card` prints the weekend quick card — the pack's cars as number → driver,
pencil blanks (MY DRIVER / FAVORITES / GROUP), stations, and the seven
rules (the writable-strip tradition: the manual ships with the pack);
`build --teach` marks a pack so the radio teaches itself (the fading
legend, scan01_lessons.c) instead of arriving quiet.
Its golden test proves the pack JSON <-> EEPROM round-trip byte-for-byte
against a fixture written by the REAL firmware pack layer.

The headless radio (tests/sim_radio.c + tests/sim_stubs.c) drives the REAL
Scan 01 UI against stubbed hardware and asserts the pixel budgets the
hardware screenshot gate was going to check — every push produces
viewable screenshots as a CI artifact (scan01-screenshots).

CI runs these on every push, then builds the firmware and checks the
flash gate.

### What's implemented (all host-verified, firmware builds green)

* **The Scan 01 interface** — SCAN/HOLD/LIST/CAPTURE/BRD/WX/SETUP, the
  32 px racing-digits font, the multi-tap name editor, the venue
  dividers, the boot identity screen. The stock ham UI is compiled out.
* **The pack layer** — EEPROM layout, the demo-pack fallback, the seal,
  PRACTICE mode, capture with the decoded tone, lockouts, my-driver.
* **The scan engine** — 80 ms dwell, tone-lock landing (foreign tones
  skipped), 250 ms hang with no-chop, the my-driver FOLLOW interleave
  (640 ms worst case), the CSQ open-mic guard, group filters (F2).
* **The packtool** — validate/build/diff/import/compose over the
  community data, dump/flash over the k5prog UART protocol.
* **The headless radio** — every screen is generated and pixel-asserted
  without hardware; the sim lands signals, captures, and scans.

### Why shouldn't I use Scan 01 yet?

* There are no stable releases yet — a rolling **user-testing**
  pre-release carries the latest build (rebuilt on every push); testers:
  see the [user testing guide](docs/user-testing.md).

### Flash from your browser (no cable drivers, no flashing software)

The easiest way to try a build: open the web flasher with the latest
binary pre-loaded —

    https://egzumer.github.io/uvtools/?firmwareURL=https://github.com/downwithbgp/scan-01/releases/download/user-testing/scan01.packed.bin

Plug the radio into USB (it boots into its built-in flasher mode), pick
the COM port, and flash. The flasher shows the boot label `Scan01 v0.1`.
This flashes whatever the rolling **user-testing** release holds at that
moment — always the newest push to main. The classic k5prog path is
documented in the [user testing guide](docs/user-testing.md).
* No hardware validation yet: the (hw) gates — real-glass screenshots,
  the dump/flash round-trip, the bench tests (docs/bench-rig.md), and the fan
  hand-off — need a radio in a hand. Everything is host-verified;
  nothing has been turned on.
* The receiver is a $25 wide-open front end. In a packed grandstand it
  will hear everything, including things you don't want. A tuned front
  end waits for the hardware v2 (vision doc §10).
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

* [User testing guide](docs/user-testing.md) — streamlined directions for testers
* [Design vision](docs/design/hci-vision.md) — the HCI reimagination
* [P1 spec — pack format and EEPROM layout](spec/p1-pack-eeprom/spec.md)
* [P2 spec — scan engine](spec/p2-scan-engine/spec.md)
* [Rams audit](docs/design/rams-audit.md) — self-assessment against the
  ten principles
