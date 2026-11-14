# Build configuration. Override any of these in config.local.mk (gitignored)
# or on the make command line.

WATCOM        ?= $(HOME)/.local/opt/watcom-20260901
DJGPP_PREFIX  ?= $(HOME)/.local/opt/djgpp-gcc1220
HOST_CC       ?= gcc
PYTHON        ?= python3

MGA_CACHE     ?= $(HOME)/.cache/mga-glide
GAMES_DIR     ?= $(HOME)/DOSGAMES
# GLOS's checkout: its headers (API 2.0) for the devices' GLOS paths (hal/src/rg).
GLOS          ?= $(HOME)/GLOS
FIXTURES_DIR  ?= $(MGA_CACHE)/fixtures
BOX86_DIR     ?= $(MGA_CACHE)/86box

# Loop A (86Box) harness.
LOOPA_JOBS    ?= 6
LOOPA_TIMEOUT ?= 300
LOOPA_IDLE    ?= 60

# Loop B (bench) and Loop C (cuda6 rig).
BENCH_SERIAL  ?= /dev/ttyS0
BENCH_BAUD    ?= 115200
RIG_HOST      ?= retro@cuda6
RIG_DIR       ?= mga-rig

# Wrapper that runs container-only tools (86Box, mtools, Xvfb).
DEV           ?= tools/dev

DEBUG         ?= 0
