#!/bin/bash
# RaceScan edition build (mirrors compile-with-docker.sh racescan())
# T1 flag policy: only disable flags the base's own editions prove safe
# (SPECTRUM/VOX/AIRCOPY/AUDIO_BAR per 'basic'); everything else stays at
# Makefile defaults — several 'creative' disables (BIG_FREQ, SCAN_RANGES)
# expose latent unconditional-reference bugs in the base.
set -e
cd /app
make \
  ENABLE_SPECTRUM=0 \
  ENABLE_FEAT_F4HWN_SPECTRUM=0 \
  ENABLE_VOX=0 \
  ENABLE_AIRCOPY=0 \
  ENABLE_AUDIO_BAR=0 \
  ENABLE_ALARM=0 \
  ENABLE_DTMF_CALLING=0 \
  ENABLE_VOICE=0 \
  ENABLE_TX_WHEN_AM=0 \
  ENABLE_F_CAL_MENU=0 \
  ENABLE_COPY_CHAN_TO_VFO=0 \
  ENABLE_FEAT_F4HWN_GAME=0 \
  ENABLE_FEAT_F4HWN_RESCUE_OPS=0 \
  ENABLE_FMRADIO=1 \
  ENABLE_NOAA=1 \
  ENABLE_FEAT_F4HWN_SCREENSHOT=1 \
  ENABLE_FEAT_F4HWN_RX_TX_TIMER=1 \
  ENABLE_FEAT_F4HWN_RESUME_STATE=1 \
  EDITION_STRING=RaceScan \
  TARGET=f4hwn.racescan \
  2>&1 | grep -v '^arm-none-eabi-gcc -c' | tail -30
arm-none-eabi-size f4hwn.racescan
