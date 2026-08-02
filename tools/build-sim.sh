#!/bin/bash
# Build + run the headless radio: the Scan 01 UI driven against stubbed
# hardware. Produces screenshots/ (P4 PBM) + the pixel-budget assertions.
# The -D list mirrors the firmware's scan01 build flags (struct layouts
# depend on them: ENABLE_FEAT_F4HWN, ENABLE_FMRADIO, ENABLE_NOAA, ...).
set -e
cd "$(dirname "$0")/.."

DEFS="-DENABLE_FEAT_SCAN01 -DENABLE_FMRADIO -DENABLE_NOAA -DENABLE_UART \
 -DENABLE_BIG_FREQ -DENABLE_SMALL_BOLD -DENABLE_TX1750 -DENABLE_KEEP_MEM_NAME \
 -DENABLE_WIDE_RX -DENABLE_NO_CODE_SCAN_TIMEOUT -DENABLE_AM_FIX \
 -DENABLE_SQUELCH_MORE_SENSITIVE -DENABLE_FASTER_CHANNEL_SCAN -DENABLE_RSSI_BAR \
 -DENABLE_SCAN_RANGES -DENABLE_FLASHLIGHT -DENABLE_CUSTOM_MENU_LAYOUT \
 -DENABLE_FEAT_F4HWN -DENABLE_FEAT_F4HWN_SCREENSHOT -DENABLE_FEAT_F4HWN_RX_TX_TIMER \
 -DENABLE_FEAT_F4HWN_SLEEP -DENABLE_FEAT_F4HWN_RESUME_STATE -DENABLE_FEAT_F4HWN_NARROWER \
 -DENABLE_FEAT_F4HWN_INV -DENABLE_FEAT_F4HWN_CTR -DENABLE_FEAT_F4HWN_CA \
 -DVERSION_STRING=\"v4.3\" -DEDITION_STRING=\"Scan01\""

gcc -Wall -Werror -Wextra $DEFS -I. \
  tests/sim_radio.c tests/sim_stubs.c scan01_ui.c scan01_keys.c scan01_edit.c \
  settings_pack.c pack_bandlock.c font_racing.c font_racing_data.c ui/helper.c \
  external/printf/printf.c font.c dcs.c \
  -o /tmp/sim_radio

/tmp/sim_radio
