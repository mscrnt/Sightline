# Makefile to build Goldeneye 007

### Default target ###
default: all

### Default Build Options ###
# Version of the game to build
FINAL := YES
VERSION := US
IDO_RECOMP := YES
VERBOSE := 2
# If COMPARE is 1, check the output sha1sum when building 'all', and if fail to match
# then compare ELF sections to known md5 checksums.
COMPARE := 1

### Sightline additions — see docs/toolchain.md ###

# Every Python invocation goes through the repo venv. Do NOT rely on
# `source .venv/bin/activate`: agent sessions get fresh non-interactive shells,
# so activation never persists and the resulting ImportErrors read as toolchain
# bugs rather than as a missing venv.
PYTHON ?= .venv/bin/python3

# $(PYTHON) only covers Python this Makefile launches directly. Upstream scripts
# that shell out to a bare `python3` would escape it, so the venv's bin goes on
# the front of PATH for the whole process tree. This is what lets bare `python3`
# resolve correctly without editing a single upstream script. Guarded on the
# activate script so that `make PYTHON=python3` cannot put the repo root on PATH.
SL_VENV_BIN := $(abspath $(dir $(PYTHON)))
ifneq ($(wildcard $(SL_VENV_BIN)/activate),)
  export PATH := $(SL_VENV_BIN):$(PATH)
endif

# The user-supplied ROM. Never committed — see .gitignore.
BASEROM ?= baserom.u.z64

# Include Terminal Codes for colourising text.
include include/make/VT100Codes.make
include include/make/Gui.make

# set toolchain based on current OS
ifeq ($(shell type mips-linux-gnu-ld >/dev/null 2>/dev/null; echo $$?), 0)
  TOOLCHAIN := mips-linux-gnu-
else ifeq ($(shell type mips64-linux-gnu-ld >/dev/null 2>/dev/null; echo $$?), 0)
  TOOLCHAIN := mips64-linux-gnu-
else
  TOOLCHAIN := mips64-elf-
endif

# Use IDO Recomp UNLESS specified otherwise
ifeq ($(IDO_RECOMP), NO)
  QEMU_IRIX := $(shell which qemu-irix 2>/dev/null)
  ifeq (, $(QEMU_IRIX))
    $(error Using the IDO compiler requires qemu-irix. Please install qemu-irix package or set the QEMU_IRIX environment variable to the full qemu-irix binary path)
  endif
  IRIX_ROOT := tools/irix/root
else
  IRIX_ROOT := tools/ido5.3_recomp
endif

# other tools
TOOLS_DIR := tools
DATASEG_COMP := $(TOOLS_DIR)/data_compress.sh
RZ_COMP := $(TOOLS_DIR)/1172compress.sh
N64CKSUM := $(TOOLS_DIR)/n64cksum

ifeq ($(VERBOSE), 1)
 SHA1SUM = sha1sum
else
 SHA1SUM = sha1sum --quiet
endif

# Convert AI Print commands from readable strings to byte arrays automatically.
ConvertAIPRINT = sed -E -e ':loop s/PRINT\("(..*?)(.)"/PRINT\("\1",\x27\2\x27/g; tloop; \
                s/(PRINT\(.*?)\x27\\\x27,\x27(.)\x27(.*)\)/\1\x27\\\2\x27\3\)/g; \
                s/(PRINT\()"(.)"(.*)\)/\1\x27\2\x27\3\)/g; \
                s/PRINT\((.*)\)/PRINT\(\1,\x27\\0\x27\,)/g; \
                s/PRINT\((.*)\)/AI_PRINT,\1/g'

# per VERSION flags
ifeq ($(FINAL), YES)
 OPTIMIZATION := -O2
 LCDEFS :=
 CFLAGWARNING :=
else
 OPTIMIZATION := -g
 LCDEFS := -DDEBUG
 CFLAGWARNING :=-fullwarn -wlint
endif

ifeq ($(VERSION), US)
 COUNTRYCODE := u
 OUTCODE := $(COUNTRYCODE)
 LANG := US
 LCDEFS := -DVERSION_US -DLANG_US -DREFRESH_NTSC -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM -DBUGFIX_R0 -DBYTEMATCH
 ASMDEFS := --defsym VERSION_US=1 --defsym LANG_US=1 --defsym REFRESH_NTSC=1 --defsym LEFTOVERDEBUG=1 --defsym LEFTOVERSPECTRUM=1 --defsym BUGFIX_R0=1 --defsym BYTEMATCH=1
 LDFILEOPTS := -DVERSION_$(LANG) -DOUTCODE=$(OUTCODE)
endif

ifeq ($(VERSION), EU)
 COUNTRYCODE := e
 OUTCODE := $(COUNTRYCODE)
 LANG := EU
 LCDEFS := -DVERSION_EU -DLANG_EU -DREFRESH_PAL -DBUGFIX_R1 -DBUGFIX_R2 -DBYTEMATCH
 ASMDEFS := --defsym VERSION_EU=1 --defsym LANG_EU=1 --defsym REFRESH_PAL=1 --defsym BUGFIX_R1=1 --defsym BUGFIX_R2=1 --defsym BYTEMATCH=1
 LDFILEOPTS := -DVERSION_$(LANG) -DOUTCODE=$(OUTCODE)
endif

ifeq ($(VERSION), JP)
 COUNTRYCODE := j
 OUTCODE := $(COUNTRYCODE)
 LANG := JP
 LCDEFS := -DVERSION_JP -DLANG_JP -DREFRESH_NTSC -DBUGFIX_R1 -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM -DBYTEMATCH
 ASMDEFS := --defsym VERSION_JP=1 --defsym LANG_JP=1 --defsym REFRESH_NTSC=1 --defsym BUGFIX_R1=1 --defsym LEFTOVERDEBUG=1 --defsym LEFTOVERSPECTRUM=1 --defsym BYTEMATCH=1
 LDFILEOPTS := -DVERSION_$(LANG) -DOUTCODE=$(OUTCODE)
endif

ifeq ($(VERSION), DEBUG)
 COUNTRYCODE := u
 OUTCODE := d
 LANG := US
 LCDEFS := -DVERSION_US -DLANG_US -DREFRESH_NTSC -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM -DBUGFIX_R0 -DDEBUGMENU -DVERSION_DEBUG
 ASMDEFS := --defsym VERSION_DEBUG=1 --defsym LANG_US=1 --defsym REFRESH_NTSC=1 --defsym LEFTOVERDEBUG=1 --defsym LEFTOVERSPECTRUM=1 --defsym BUGFIX_R0=1 --defsym DEBUGMENU=1
 COMPARE := 0
 LDFILEOPTS := -DVERSION_$(LANG) -DOUTCODE=$(OUTCODE)
endif

ifeq ($(VERSION), USB)
 COUNTRYCODE := u
 OUTCODE := usb
 LANG := US
 LCDEFS := -DVERSION_US -DLANG_US -DREFRESH_NTSC -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM -DBUGFIX_R0 -DDEBUGMENU -DENABLE_USB
 ASMDEFS := --defsym VERSION_US=1 --defsym LANG_US=1 --defsym REFRESH_NTSC=1 --defsym LEFTOVERDEBUG=1 --defsym LEFTOVERSPECTRUM=1 --defsym BUGFIX_R0=1 --defsym DEBUGMENU=1 --defsym ENABLE_USB=1
 COMPARE := 0
 LDFILEOPTS := -DVERSION_$(LANG) -DOUTCODE=$(OUTCODE) -DENABLE_USB
endif

ALLOWED_VERSIONS := US EU JP DEBUG USB
ALLOWED_COUNTRYCODE := u e j

BUILD_DIR_BASE := build
# BUILD_DIR is the location where all build artefacts are placed
BUILD_DIR      := $(BUILD_DIR_BASE)/$(OUTCODE)

# this file references variables defined above: BUILD_DIR, RZ_COMP
# this file defines and builds $(MUSIC_RZ_FILES)
include assets/Makefile.obseg
# this file references variables defined above: BUILD_DIR, RZ_COMP, COUNTRYCODE, LD, CC, CFLAGS, OBJCOPY, ConvertAIPRINT, OPTIMIZATION
# this file defines and builds OBSEGMENT, BG_SEG_FILES, BRIEF_RZ_FILES, CHR_RZ_FILES, GUN_RZ_FILES, PROP_RZ_FILES, ,SETUP_BUILD_FILES, STAN_BUILD_FILES, TEXT_RZ_FILES
include assets/Makefile.music

## Collect Objects ##

APPELF := $(BUILD_DIR)/ge007.$(OUTCODE).elf
APPROM := $(BUILD_DIR)/ge007.$(OUTCODE).z64
APPBIN := $(BUILD_DIR)/ge007.$(OUTCODE).bin

HEADERFILES := $(foreach dir,src,$(wildcard $(dir)/*.s))
HEADEROBJECTS := $(foreach file,$(HEADERFILES),$(BUILD_DIR)/$(file:.s=.o))

RSPCODE := $(foreach dir,rsp,$(wildcard $(dir)/*.s))
RSPOBJECTS := $(foreach file,$(RSPCODE),$(BUILD_DIR)/$(file:.s=.bin))

CODEFILES := $(foreach dir,src,$(wildcard $(dir)/*.c))
CODEOBJECTS := $(foreach file,$(CODEFILES),$(BUILD_DIR)/$(file:.c=.o))

GAMEFILES_C := $(foreach dir,src/game,$(wildcard $(dir)/*.c))
GAMEFILES_S := $(foreach dir,src/game,$(wildcard $(dir)/*.s))
GAMEOBJECTS := $(foreach file,$(GAMEFILES_S),$(BUILD_DIR)/$(file:.s=.o)) \
				$(foreach file,$(GAMEFILES_C),$(BUILD_DIR)/$(file:.c=.o))


ASSET_DATAFILES := assets/oddtextures.c assets/animationtable_data.c assets/animationtable_entries.c assets/font_dl.c assets/font_chardataj.c assets/font_chardatae.c assets/rarewarelogo.c
ASSET_DATAOBJECTS := $(foreach file,$(ASSET_DATAFILES),$(BUILD_DIR)/$(file:.c=.o))

ROMFILES2 := assets/romfiles2.s
ROMOBJECTS2 := $(BUILD_DIR)/assets/romfiles2.o

RAMROM_FILES := assets/ramrom/ramrom.s
RAMROM_OBJECTS := $(BUILD_DIR)/assets/ramrom/ramrom.o


FONTFILES_C := $(foreach dir,assets/font,$(wildcard $(dir)/*.c))
FONTOBJECTS := $(foreach file,$(FONTFILES_C),$(BUILD_DIR)/$(file:.c=.o))


MUSIC_FILES := $(foreach dir,assets/music,$(wildcard $(dir)/*.s))
MUSIC_OBJECTS := $(foreach file,$(MUSIC_FILES),$(BUILD_DIR)/$(file:.s=.o))

OBSEG_FILES := assets/obseg/ob_seg.s
OBSEG_OBJECTS := $(BUILD_DIR)/assets/obseg/ob_seg.o
OBSEG_RZ := $(BG_SEG_FILES) $(CHR_RZ_FILES) $(GUN_RZ_FILES) $(PROP_RZ_FILES) $(STAN_RZ_FILES) $(BRIEF_RZ_FILES) $(SETUP_RZ_FILES) $(TEXT_RZ_FILES)

IMAGE_BINS := assets/images/combined/combined.bin
IMAGE_OBJS := $(foreach file,$(IMAGE_BINS),$(BUILD_DIR)/$(file:.bin=.o))

RZFILES := inflate/inflate.c
RZOBJECTS := $(foreach file,$(RZFILES),$(BUILD_DIR)/src/$(file:.c=.o))

OBJECTS := $(RSPOBJECTS) $(CODEOBJECTS) $(GAMEOBJECTS) $(RZOBJECTS) $(OBSEGMENT) $(ROMOBJECTS) $(RAMROM_OBJECTS) $(FONTOBJECTS) $(MUSIC_OBJECTS) $(IMAGE_OBJS)

## Command Line args for builders ##

MIPSISET := -mips2 -32

INCLUDE := -I . -I include -I include/ultra64 -I include/PR -I src -I src/game -I src/inflate

# ignore warnings:
# 609 : The number of arguments in the macro invocation does not match the definition - disabled because CPPLib uses "VarArgs" which wasnt invented till c99
# 649 : Missing member name in structure / union                                      - used for "Inheritance"
# 709 : Incompatible pointer type assignment                                          - could be fixed by casting, but implicit is fine.
# 712 : illegal combination of pointer and integer                                    - could be fixed by casting, but implicit is fine.
# 807 : member cannot be of function or incomplete type                               - Variable length structs
# 838 : Microsoft extension (unnamed structs)                                         - used for "Inheritance" and member/array call swapping
# 763 : Max Float
WOFF :=  -woff 609,649,709,712,807,838,763

ifeq ($(IDO_RECOMP), NO)
  CC := $(QEMU_IRIX) -silent -L $(IRIX_ROOT) $(IRIX_ROOT)/usr/bin/cc
else
  CC := $(IRIX_ROOT)/cc
endif

CFLAGS := -Wab,-r4300_mul -non_shared -Olimit 2000 -G 0 -Xcpluscomm $(CFLAGWARNING) $(WOFF) $(INCLUDE) $(MIPSISET) $(LCDEFS) -DTARGET_N64

LD := $(TOOLCHAIN)ld
LD_SCRIPT := $(BUILD_DIR)/ge007.$(OUTCODE).ld

# --no-warn-mismatch is needed to link -mips3 object files (some libultra math) with the regular files compiled with -mips2
LDFLAGS := -T $(LD_SCRIPT) -Map $(BUILD_DIR)/ge007.$(OUTCODE).map --no-warn-mismatch

AS := $(TOOLCHAIN)as
ASFLAGS := -march=vr4300 -mabi=32 $(INCLUDE) $(ASMDEFS)
# Use the system installed armips if available. Otherwise use the one provided with this repository.
ifneq (,$(shell which armips 2>/dev/null))
  ARMIPS              := armips
else
  ARMIPS              := $(TOOLS_DIR)/armips
endif

OBJCOPY := $(TOOLCHAIN)objcopy












## Build Recipes ##

# Don't delete intermediate files from these targets on make completion.
.SECONDARY:
	$(APPELF) $(APPROM) $(APPBIN) $(ULTRAOBJECTS) $(BUILD_DIR)/ge007.$(OUTCODE).map \
	$(HEADEROBJECTS) $(BOOTOBJECTS) $(CODEOBJECTS) $(GAMEOBJECTS) $(RZOBJECTS) \
	$(OBSEG_OBJECTS) $(OBSEG_RZ) $(ROMOBJECTS) $(RAMROM_OBJECTS) $(FONTOBJECTS) $(MUSIC_OBJECTS) $(IMAGE_OBJS) $(MUSIC_RZ_FILES)

# Don't delete these intermediate targets on make cancellation.
.PRECIOUS: %.bin  %.o

# Run the following targets sequentially in this order (unnamed targets will still run in parallel)
.NOTPARALLEL: print_info create_directories $(APPROM) checksum

# Phony Recipes - These targets are not files, Get Make to do something
.PHONY: print_info create_directories build_tools prerequisites checksum all_p1 all default commonclean setupclean stanclean dataclean libultraclean codeclean clean nuke help cmdbuidler test  context extractassets forceextractassets textures convert_props convert_chrs convert_guns extract_u extract_e extract_j force_extract_u force_extract_e force_extract_j extract_rsp matching check-layering check-layering-baseline check-python venv verify-rom trace-verify trace-record trace-record-debug trace-diff trace-roundtrip trace-unlock direct-boot trace-gate trace-gate-status trace-rebaseline trace-debug trace-debug-verify native-skeleton demo bootsweep bootsweep-random


# this file references variables defined above: BUILD_DIR, CFLAGWARNING, INCLUDE, LCDEFS
# this file defines and builds $(ULTRAOBJECTS)
include src/libultrare/Makefile.libultrare

# Build RSP
$(BUILD_DIR)/rsp/%.bin: rsp/*.s
	$(ARMIPS) -sym $@.sym -strequ CODE_FILE $(BUILD_DIR)/rsp/$*.bin -strequ DATA_FILE $(BUILD_DIR)/rsp/$*_data.bin $<

$(BUILD_DIR)/src/rspboot.o: $(BUILD_DIR)/rsp/rspboot.bin

#Build asm files in root
$(BUILD_DIR)/%.o: src/%.s
	$(AS) $(ASFLAGS) -o $@ $<

#Build asm files in src/
$(BUILD_DIR)/src/%.o: src/%.s
	$(AS) $(ASFLAGS) -o $@ $<

#Build Images
# Generate imagelist by syncing imagelist.u.csv (ROM offsets/sizes) with images.def (names)
$(BUILD_DIR)/imagelist.csv: imagelist.u.csv assets/images.def
	@mkdir -p $(BUILD_DIR)
	$(PYTHON) scripts/make/sync_imagelist_with_def.py $@

assets/images/combined/combined.bin: $(BUILD_DIR)/imagelist.csv
	scripts/make/combine_images_named.sh $(BUILD_DIR)/imagelist.csv assets/images/combined

$(BUILD_DIR)/assets/images/combined/%.o: assets/images/combined/combined.bin
	$(LD) -r -b binary $< -o $@


#Compress Obseg
$(BUILD_DIR)/$(OBSEGMENT): $(OBSEG_RZ) $(IMAGE_OBJS)


#Build C files in src/
# convert AI_PRINT commands from readable to byte-array
$(BUILD_DIR)/src/%.o: src/%.c
	@if [ "$$(basename $<)" = "chraidata.c" ]; then \
		$(ConvertAIPRINT) $< | $(CC) -c $(CFLAGS) tools/include-stdin.c -o $@ $(OPTIMIZATION); \
	else \
		$(CC) -c $(CFLAGS) -o $@ $(OPTIMIZATION) $<; \
	fi


#Build RamRom
$(BUILD_DIR)/assets/ramrom/%.o: assets/ramrom/%.s
	$(AS) $(ASFLAGS) -o $@ $<

#Build fonts
$(BUILD_DIR)/assets/font/%.o: assets/font/%.c
	$(CC) -c $(CFLAGS) -o $@ $(OPTIMIZATION) $<

#Build asm files in assets/
$(BUILD_DIR)/assets/%.o: assets/%.s
	$(AS) $(ASFLAGS) -o $@ $<

#Build Obseg
$(BUILD_DIR)/assets/obseg/%.o: assets/obseg/%.s $(OBSEG_RZ)
	$(AS) $(ASFLAGS) -o $@ $<

#Build C files in assets/
$(BUILD_DIR)/assets/%.o: assets/%.c
ifeq ($(filter-out %setup%,$<),)
	$(ConvertAIPRINT) $< | $(CC) -c $(CFLAGS) tools/include-stdin.c -o $@ $(OPTIMIZATION)
else
	$(CC) -c $(CFLAGS) -o $@ $(OPTIMIZATION) $<
endif

#$(BUILD_DIR)/src/random.o: OPTIMIZATION := -O3
#$(BUILD_DIR)/src/random.o: INCLUDE := -I . -I include -I include/PR
#$(BUILD_DIR)/src/random.o: MIPSISET := -mips3 -o32
#$(BUILD_DIR)/src/random.o: src/random.c
#	$(CC) -c -Wab,-r4300_mul -non_shared -G 0 -Xcpluscomm $(CFLAGWARNING) -woff 819,820,852,821,838,649 -signed $(INCLUDE) $(MIPSISET) $(LCDEFS) -DTARGET_N64 $(OPTIMIZATION) -o $@ $<

#Link Files
$(APPELF): $(RSPOBJECTS) $(ULTRAOBJECTS) $(HEADEROBJECTS) $(OBSEG_RZ) $(BUILD_DIR)/$(OBSEGMENT) $(MUSIC_RZ_FILES) $(BOOTOBJECTS) $(CODEOBJECTS) $(GAMEOBJECTS) $(RZOBJECTS) $(ROMOBJECTS) $(ASSET_DATAOBJECTS) $(ROMOBJECTS2) $(RAMROM_OBJECTS) $(FONTOBJECTS) $(MUSIC_OBJECTS) $(OBSEG_OBJECTS) ge007.ld
	cpp $(LDFILEOPTS) -P ge007.ld -o $(BUILD_DIR)/ge007.$(OUTCODE).ld
	@echo "Linking Files into ELF"
	$(LD) $(LDFLAGS) -o $@

$(APPBIN): $(APPELF)
	@echo "Building ROM"
	$(OBJCOPY) $< $@ -O binary --gap-fill=0xff

$(APPROM):	$(APPBIN)
	@echo "Compressing ROM"
	$(DATASEG_COMP) $< $(OUTCODE)
	@echo "Finalizing ROM"
	$(N64CKSUM) $< $@


## Phony Recipes below - Get Make to do something ##

print_info:
	$(info VERSION=$(VERSION))
	$(info Building $(VERSION) ROM...)

create_directories:
	scripts/make/create_directories.sh "$(BUILD_DIR)" "$(COUNTRYCODE)"

build_tools:
	$(info Building tools...)
	scripts/make/build_tools.sh "$(MAKE)"

prerequisites: print_info create_directories build_tools extractassets

combine_images: assets/images/combined/combined.bin

checksum: $(APPROM)
ifeq ($(COMPARE), 1)
	scripts/make/checksum.sh "$(SHA1SUM)" "$(OUTCODE)" "$(BUILD_DIR)"
endif

all_p1: prerequisites
all: all_p1 $(APPROM) checksum
	@echo "Rom File Generated in Build Directory."

### Sightline targets — see docs/toolchain.md ###

# Guard: every Python-using target routes through $(PYTHON). Fail with the fix
# rather than with an ImportError from whichever python3 happens to be on PATH.
check-python:
	@test -x "$(PYTHON)" || { \
	  echo ""; \
	  echo "Missing Python interpreter: $(PYTHON)"; \
	  echo ""; \
	  echo "Create it with:   make venv"; \
	  echo ""; \
	  echo "Do not 'source .venv/bin/activate' instead - activation does not"; \
	  echo "persist into fresh non-interactive shells and the resulting errors"; \
	  echo "look like toolchain bugs. The Makefile always uses \$$(PYTHON)."; \
	  echo ""; \
	  exit 1; \
	}

# Create the venv and install pinned tooling. Uses the SYSTEM python, by
# necessity - this is the one place a bare interpreter is correct.
SYSTEM_PYTHON ?= python3
venv:
	$(SYSTEM_PYTHON) -m venv .venv
	.venv/bin/python3 -m pip install --upgrade pip
	.venv/bin/python3 -m pip install -r requirements.txt
	@echo ""
	@echo "venv ready at .venv - do NOT activate it; make uses \$$(PYTHON)."

# Verify the user-supplied base ROM before anything is built. A byte-swapped or
# wrong-region dump otherwise fails deep in the build looking like a toolchain
# problem. Runs standalone: make verify-rom
verify-rom:
	@tools/sightline/verify_rom.sh "$(BASEROM)" "ge007.$(OUTCODE).sha1" "$(OUTCODE)"

# The matching build: the original decomp compiled to a byte-identical ROM.
# This is the correctness oracle and is kept buildable permanently
# (docs/project-rules.md, Glossary).
# COMPARE=1 makes the build verify its own output SHA-1.
matching: verify-rom
	@# Two-phase, and deliberately so. Asset extraction WRITES source files that
	@# the main dependency graph then globs (assets/music/*.bin and friends).
	@# Under -j, make evaluates the ROM's prerequisites before extraction has
	@# produced them and fails with a bare "No rule to make target
	@# build/u/assets/music/*.rz" - which reads as a broken Makefile rather than
	@# as a race. Extract to completion first, then build.
	@$(MAKE) --no-print-directory all_p1 VERSION=$(VERSION)
	@$(MAKE) --no-print-directory all VERSION=$(VERSION) COMPARE=1

# Layering check, reporting mode. Fails only on violations absent from
# docs/layering-baseline.txt; prints the remaining count on every run.
check-layering: check-python
	@$(PYTHON) tools/sightline/check_layering.py

# Regenerate the baseline. Legitimate ONLY when the count goes DOWN.
check-layering-baseline: check-python
	@$(PYTHON) tools/sightline/check_layering.py --write-baseline

### Sightline trace harness (Phase 0) — see tools/trace/README.md ###

TRACE_DIR   := tools/trace/traces
TRACE_INPUTS := tools/trace/inputs
# Persistent cartridge save. Without one the game is a fresh cartridge on every
# run - only the first mission available, nothing you play remembered - which
# makes recording later levels impossible. Never committed: it is your progress,
# and it changes every session.
SAVE_FILE ?= tools/trace/saves/cartridge.sav
TRACE_TOOL  := tools/trace/trace.py
TRACE_TICKS ?= 300

# A real gfx plugin is required for the game to run at all, so a display is
# required. Use the ambient one if present, otherwise a virtual one. Hashes are
# identical either way (verified), so CI results compare directly to local runs.
ifeq ($(strip $(DISPLAY)),)
  XVFB := xvfb-run -a --server-args=-screen\ 0\ 640x480x24
else
  XVFB :=
endif

# Replay every recorded trace and compare state hashes, or one with LEVEL=<name>.
trace-verify: check-python verify-rom
	@set -e; \
	if [ -n "$(LEVEL)" ]; then \
	  list="$(TRACE_DIR)/$(LEVEL).sltrace"; \
	  [ -f "$$list" ] || { echo "no recorded trace for LEVEL=$(LEVEL)"; exit 1; }; \
	else \
	  list=$$(ls $(TRACE_DIR)/*.sltrace 2>/dev/null || true); \
	fi; \
	if [ -z "$$list" ]; then \
	  echo "No traces recorded yet - nothing to verify."; \
	  echo "Record one with: make trace-record LEVEL=<name>"; \
	  exit 0; \
	fi; \
	fail=0; \
	for t in $$list; do \
	  lvl=$$(basename $$t .sltrace); \
	  in=$(TRACE_INPUTS)/$$lvl.input; \
	  if [ ! -f "$$in" ]; then \
	    echo "  $$lvl: NO INPUT STREAM ($$in) - re-record with 'make trace-record LEVEL=$$lvl'"; \
	    fail=1; continue; \
	  fi; \
	  tmp=$$(mktemp -t sl-$$lvl-XXXX.sltrace); \
	  snap=$(TRACE_INPUTS)/$$lvl.eeprom; \
	  save=""; [ -f "$$snap" ] && save="--eeprom $$snap"; \
	  romopt=""; \
	  pick=$$($(PYTHON) $(TRACE_TOOL) rom-for "$$t" build/u/direct/ge007.u.$$lvl.z64 $(BUILD_DIR)/ge007.$(OUTCODE).z64 2>/dev/null); \
	  [ -n "$$pick" ] && romopt="--rom $$pick"; \
	  $(XVFB) $(PYTHON) $(TRACE_TOOL) capture --out $$tmp --level $$lvl \
	      --replay "$$in" $$save $$romopt >/dev/null 2>&1 || { echo "capture FAILED: $$lvl"; fail=1; rm -f $$tmp; continue; }; \
	  if $(PYTHON) $(TRACE_TOOL) diff $$t $$tmp; then \
	    echo "  $$lvl: OK"; \
	  else \
	    fail=1; \
	  fi; \
	  rm -f $$tmp; \
	done; \
	exit $$fail

# Build a ROM that boots straight into a stage on Agent, skipping the menus.
# bossMainloop already does the work when g_StageNum is not the title; setting
# the INITIALISER is what makes it reachable, since the value must be in place
# before the first instruction runs. Cached per level: the segment holding it
# is compressed, so one word moves ~58KB and there is nothing to patch in a
# finished image. usage: make direct-boot LEVEL=facility
#
# ALWAYS restores the matching build afterwards. build/u/ge007.u.z64 is what
# the harness reads by default, and leaving a direct-boot ROM there would
# quietly record every later trace against the wrong image.
# DIFFICULTY defaults to agent, which is what the direct-boot path selects on
# its own; anything else adds SL_DIRECT_BOOT_DIFFICULTY and gets its own cached
# ROM so Agent recordings keep matching their image by hash.
DIFFICULTY ?= agent
# TOUGH=<n> records an AI OBSERVATION run rather than a parity run: it scales
# Bond's health by n so a whole level can be walked on any difficulty to watch
# spawns, accuracy, alarms and squad reaction, without dying limiting coverage.
#
# NOT the invincibility cheat. That flag is tested after damage is computed, so
# hits still register - but health never moves, and health is what the trace
# records, making accuracy invisible in the very runs made to measure it.
# 007-mode sliders. Range 0..10 for accuracy/damage/health, 0..1 for reaction;
# defaults (1/1/1/0) are what makes an unconfigured 007 measure like 00 Agent.
S007_ACCURACY ?=
S007_DAMAGE ?=
S007_HEALTH ?=
S007_REACTION ?=
TOUGH ?=
# ALLY_INVINCIBLE=1 makes escort NPCs (Natalya) immune - observation runs only.
ALLY_INVINCIBLE ?=
# ROM_DEBUG=1 builds the ORACLE ROM: the same direct-boot image plus
# SIGHTLINE_ROM_DEBUG, which compiles in the debug mailbox (src/game/lv.c, see
# src/game/sl_romdbg.h). It is knowingly NON-MATCHING and exists only to
# explain state. It gets its own cached image, `.dbg.z64`, so a debug ROM can
# never be mistaken for the parity ROM a trace was recorded against.
#
#   Normal ROM proves behaviour. Debug ROM explains state.
#
# `make matching` is untouched by this: every line the flag adds is inside an
# #ifdef, so the preprocessor deletes all of it from the matching build.
ROM_DEBUG ?=
# ROM_SIZECTL=1 is the CONTROL for ROM_DEBUG, and it is not optional reading.
# The game seeds its RNG from the CPU cycle counter at boot
# (boss.c:389 `nowCount = osGetCount(); randomSetSeed(nowCount);`), so ANY
# change to the ROM image - even one that never executes - lands a different
# seed and every trace after it diverges. Without a control you cannot tell
# "my instrument perturbs the game" from "the image is a different size".
#
# This flag adds inert padding and NO code. A ROM built with it is a normal
# game whose image differs by about as much as the debug ROM's does. If its
# trace diverges the same way, the divergence is a property of the image, not
# of the instrument. See docs/backlog.md, B-051.
ROM_SIZECTL ?=
# ROM_AUDIO_EVENTS=1 compiles in the audio EVENT LOG (src/game/sl_audioev.h):
# the frame-counter writer as a positive control, the parsed note-on tuple,
# effects entry, and the effect-count increment/decrement. Like ROM_DEBUG it is
# knowingly NON-MATCHING and gets its own cached image, `.aev.z64`.
#
# Its output is valid for THE RUN IT PRODUCES and never for the parity run:
# boss.c:389 seeds the RNG from osGetCount() and the main loop is cycle-paced,
# so any image change moves the seed and any added work changes the run. What
# licenses using it anyway is measured separately - the complete ordered
# note-on population is byte-identical across seeds whose game-state traces
# differ. See docs/backlog.md.
ROM_AUDIO_EVENTS ?=
# ROM_CODECTL=1 is the CONTROL FOR ROM_AUDIO_EVENTS' CODE FOOTPRINT, and it is
# a different control from ROM_SIZECTL. ROM_SIZECTL's padding is rodata in
# lv.c, which lives in the TLB-paged .game segment: it grows _bssSegmentEnd and
# leaves .code alone. The audio path is entirely in .code, the resident
# segment, so instrumenting it grows .code and shifts every segment after it.
# This flag adds inert, never-called .text to audi.c and nothing else, so an
# image built with it isolates ".code grew" from "the instrument ran".
# See src/audi.c and docs/backlog.md.
ROM_CODECTL ?=
# NAME is the recording's identity, defaulting to the level. Give a sweep its
# own NAME so it does not overwrite the level's parity recording - and because
# the ROM is cached under NAME, every existing hash-matching lookup keeps
# working with no special cases.
SL_NAME ?= $(LEVEL)
# Frames before a recording stops itself. 18000 = 5 minutes at 60fps.
RECORD_TICKS ?= 200000
# NOTE: every file that consumes a direct-boot define must appear in the
# `touch` lists inside this recipe. LCDEFS changes do NOT invalidate object
# files - make compares timestamps only - so an unlisted file silently links
# the object built without the define. That is why ALLY_INVINCIBLE appeared
# to do nothing at first: chraction.o was current from the matching build.
#
# The same hazard exists one level up, in the ROM cache below. The cache key
# was the SETTINGS SPEC ONLY, which says nothing about the source. A ROM built
# before a source fix, stamped with a spec that still matches, is served as
# "cached" and the fix silently never reaches the player - measured: a ROM
# stamped ALLY=1 that was byte-identical to an ALLY-less build. So the spec
# now carries SRC=, a digest of every .c/.h under src/.
#
# It must be a CONTENT digest, not an mtime comparison: this recipe ends by
# touching the sources above to restore the matching build, so after any build
# the sources are always newer than the ROM it just produced, and an
# mtime-based check would rebuild every single time.
direct-boot: check-python verify-rom
	@test -n "$(LEVEL)" || { echo "usage: make direct-boot LEVEL=<name> [DIFFICULTY=00] [TOUGH=100] [SL_NAME=id]"; exit 1; }
	@out=build/u/direct/ge007.u.$(SL_NAME)$(if $(ROM_DEBUG),.dbg,)$(if $(ROM_SIZECTL),.ctl,)$(if $(ROM_AUDIO_EVENTS),.aev,)$(if $(ROM_CODECTL),.cctl,).z64; \
	srchash=$$(find src -type f \( -name '*.c' -o -name '*.h' \) -print0 | sort -z | xargs -0 sha1sum | sha1sum | cut -c1-12); \
	spec="LEVEL=$(LEVEL) DIFFICULTY=$(DIFFICULTY) TOUGH=$(TOUGH) ALLY=$(ALLY_INVINCIBLE) S007=$(S007_ACCURACY)/$(S007_DAMAGE)/$(S007_HEALTH)/$(S007_REACTION) ROMDBG=$(ROM_DEBUG) SIZECTL=$(ROM_SIZECTL) AEV=$(ROM_AUDIO_EVENTS) CODECTL=$(ROM_CODECTL) SRC=$$srchash"; \
	if [ -f "$$out" ] && [ "$$(cat $$out.build 2>/dev/null)" = "$$spec" ]; then \
	  echo "  cached: $$out"; exit 0; \
	fi; \
	if [ -f "$$out" ]; then \
	  echo "  cached ROM was built with [$$(cat $$out.build 2>/dev/null || echo unknown)],"; \
	  echo "  now asked for [$$spec] - rebuilding."; \
	fi; \
	id=$$($(PYTHON) -c "import sys;sys.path.insert(0,'tools/trace');from sltrace.levelboot import level_id;print(level_id('$(LEVEL)'))") || exit 1; \
	echo "  building direct-boot ROM for $(LEVEL) (LEVELID $$id)"; \
	mkdir -p build/u/direct; \
	touch src/boss.c src/game/player.c src/game/lv.c src/game/initgamedata.c \
	      src/game/chraction.c src/game/chrai.c \
	      src/audi.c src/snd.c src/music.c src/libultra/audio/cseq.c; \
	dflag=""; \
	if [ "$(DIFFICULTY)" != "agent" ]; then \
	  did=$$($(PYTHON) -c "import sys;sys.path.insert(0,'tools/trace');from sltrace.levelboot import difficulty_id;print(difficulty_id('$(DIFFICULTY)'))") || exit 1; \
	  dflag=" -DSL_DIRECT_BOOT_DIFFICULTY=$$did"; \
	fi; \
	$(if $(TOUGH),dflag="$$dflag -DSL_BOND_HEALTH_MULT=$(TOUGH)";,) \
	$(if $(ALLY_INVINCIBLE),dflag="$$dflag -DSL_ALLY_INVINCIBLE";,) \
	$(if $(ROM_DEBUG),dflag="$$dflag -DSIGHTLINE_ROM_DEBUG";,) \
	$(if $(ROM_SIZECTL),dflag="$$dflag -DSL_ROMDBG_SIZECTL";,) \
	$(if $(ROM_AUDIO_EVENTS),dflag="$$dflag -DSIGHTLINE_AUDIO_EVENTS";,) \
	$(if $(ROM_CODECTL),dflag="$$dflag -DSL_AUDIOEV_CODECTL";,) \
	$(if $(S007_ACCURACY),dflag="$$dflag -DSL_007_ACCURACY=$(S007_ACCURACY)";,) \
	$(if $(S007_DAMAGE),dflag="$$dflag -DSL_007_DAMAGE=$(S007_DAMAGE)";,) \
	$(if $(S007_HEALTH),dflag="$$dflag -DSL_007_HEALTH=$(S007_HEALTH)";,) \
	$(if $(S007_REACTION),dflag="$$dflag -DSL_007_REACTION=$(S007_REACTION)";,) \
	$(MAKE) --no-print-directory all LCDEFS="$(LCDEFS) -DSL_DIRECT_BOOT_LEVEL=$$id$$dflag" COMPARE=0 >/dev/null || exit 1; \
	cp $(BUILD_DIR)/ge007.$(OUTCODE).z64 "$$out"; \
	cp $(BUILD_DIR)/ge007.$(OUTCODE).map "$${out%.z64}.map"; \
	if cmp -s "$$out" $(BASEROM); then \
	  rm -f "$$out"; \
	  echo "::error:: built ROM is identical to the unmodified one - the define did not take."; \
	  echo "  (this is what 'make -n' produces: it runs recipe lines containing \$$(MAKE),"; \
	  echo "   the inner builds inherit -n and do nothing, and the cp copies an unbuilt ROM)"; \
	  exit 1; \
	fi; \
	echo "$$spec" > "$$out.build"; \
	echo "  built $$out"; \
	echo "  restoring the matching build"; \
	touch src/boss.c src/game/player.c src/game/lv.c src/game/initgamedata.c \
	      src/game/chraction.c src/game/chrai.c \
	      src/audi.c src/snd.c src/music.c src/libultra/audio/cseq.c; \
	$(MAKE) --no-print-directory matching >/dev/null || exit 1

# Build the ORACLE ROM for a recording: the same direct-boot image plus
# SIGHTLINE_ROM_DEBUG. Writes build/u/direct/ge007.u.<SL_NAME>.dbg.z64 and its
# map, alongside - never over - the parity ROM the recording was made against.
#
#   make trace-debug LEVEL=facility SL_NAME=facility-pane
#   tools/native/rominspect.py --input facility-pane --select-at 7000
#
# The acceptance test for the whole mechanism is that the debug ROM reproduces
# the NORMAL ROM's state trace for the same input stream. If it does not, the
# instrument is perturbing the game and no capture it produces can be trusted:
#
#   make trace-debug-verify LEVEL=facility SL_NAME=facility-pane
trace-debug: check-python verify-rom
	@test -n "$(LEVEL)" || { echo "usage: make trace-debug LEVEL=<name> [SL_NAME=id]"; exit 1; }
	@$(MAKE) --no-print-directory direct-boot ROM_DEBUG=1 LEVEL=$(LEVEL) \
	    SL_NAME=$(SL_NAME) DIFFICULTY=$(DIFFICULTY) TOUGH=$(TOUGH)

# Replay one recording against the DEBUG ROM and diff the resulting state trace
# against the one the recording carries (which the normal ROM produced). This
# is control 1: an oracle that explains a different game than the one being
# measured is worse than no oracle at all.
#
# The debug trace goes to build/, NEVER to $(TRACE_DIR): trace-verify globs
# $(TRACE_DIR)/*.sltrace and treats every file it finds as a LEVEL, so a
# `facility-pane.dbg.sltrace` sitting there fails the whole gate looking for a
# `facility-pane.dbg.input` that does not exist. Measured the hard way.
trace-debug-verify: check-python trace-debug
	@mkdir -p build/trace
	@$(PYTHON) $(TRACE_TOOL) capture \
	    --out build/trace/$(SL_NAME).dbg.sltrace \
	    --level $(SL_NAME) \
	    --rom build/u/direct/ge007.u.$(SL_NAME).dbg.z64 \
	    --map build/u/direct/ge007.u.$(SL_NAME).dbg.map \
	    --replay $(TRACE_INPUTS)/$(SL_NAME).input \
	    --eeprom $(TRACE_INPUTS)/$(SL_NAME).eeprom
	@$(PYTHON) $(TRACE_TOOL) diff --across-roms $(TRACE_DIR)/$(SL_NAME).sltrace \
	    build/trace/$(SL_NAME).dbg.sltrace

# Regenerate baselines by replaying the recordings that produced them. Needed
# after a schema change; the recordings themselves are unaffected, so no level
# is ever replayed by hand. usage: make trace-rebaseline [JOBS=5]
JOBS ?= 1
trace-rebaseline: check-python
	@$(PYTHON) tools/trace/rebaseline.py --jobs $(JOBS) $(if $(LEVEL),--level $(LEVEL),)

# The Phase 0 gate: every recorded level replays byte-identically, RUNS times.
# One clean pass proves little - the save write-back bug passed run 1 and only
# corrupted run 2, and mupen64plus reproduced about one replay in four.
#
# Results are keyed to the CONTENT of each recording, so a level that has
# passed is skipped until its recording changes. Adding levels costs only the
# new ones. usage: make trace-gate [RUNS=10] [LEVEL=x] / make trace-gate-status
RUNS ?= 10
trace-gate: check-python
	@$(PYTHON) tools/trace/gate.py --runs $(RUNS) --jobs $(JOBS) $(if $(LEVEL),--level $(LEVEL),)

trace-gate-status: check-python
	@$(PYTHON) tools/trace/gate.py --status

# Native skeleton: the whole sim compiled to host objects and linked with a
# seven-function libultra shim plus auto-generated stubs for everything not
# yet ported. See tools/native/build.sh and docs/decisions/phase1-tracks.md.
native-skeleton:
	@tools/native/build.sh

# THE health bar for anything that enables native audio. 900 frames of facility,
# because that is the frame count the cartridge ACMD capture uses - the parity
# requirement and the survival requirement are the same number, and treating
# them as different is how a build that faults at frame 501 was landed on a
# 60-frame check. Prints the frame count REACHED, so a short run cannot be
# mistaken for a clean one.
native-health: native-skeleton
	@tools/native/health.sh

# Play it. Window, real-time pacing, no frame limit, live keyboard and mouse -
# the flags a player wants rather than the ones trace replay wants. Level data
# comes from your own ROM at runtime; SL_LEVEL and SL_DIFFICULTY override.
demo:
	@tools/native/play.sh

# Boot every campaign level natively. A smoke test, not a gate: it answers
# "does the sim come up" per level, which a harness pointed at one level
# cannot see. Exits non-zero with the count of levels that failed.
bootsweep:
	@tools/native/bootsweep.sh

# Stochastic coverage rather than a gate: no pinned seed, so each level boots
# on a host-clock RNG state. A failure prints the seed that produced it.
bootsweep-random:
	@SL_SWEEP_RANDOM=1 tools/native/bootsweep.sh

# Seed the cartridge save so every stage is selectable. Reaching a late level
# otherwise means completing every level before it first, which is not a
# reasonable prerequisite for recording a trace. Verifies by booting with the
# seeded save and reading progress back out of live RAM - a bad checksum makes
# the game reset the slot silently, so checking our own file would prove
# nothing. usage: make trace-unlock
trace-unlock: check-python verify-rom
	@mkdir -p $(dir $(SAVE_FILE))
	@$(PYTHON) $(TRACE_TOOL) unlock --eeprom $(SAVE_FILE)

# Play a level and record it. Opens a WINDOW and needs a pad - deliberately not
# wrapped in $(XVFB), because the whole point is that you can see what you are
# recording. Writes the input stream and the state trace it produced; close the
# window to stop.
#
# Also writes a .spec sidecar: the build recipe this recording was made with.
# An input stream is only replayable against the ROM it was recorded on - a run
# captured with ALLY_INVINCIBLE=1, where Natalya survives 13 hits, desynchronises
# completely against a build where she dies at the seventh. /build/ is gitignored,
# so without this sidecar the recipe lives nowhere durable and `make clean` loses
# it. SRC= is stripped: it fingerprints the source tree, which legitimately moves
# on, and is not part of the recipe.
trace-record: check-python verify-rom
	@test -n "$(LEVEL)" || { echo "usage: make trace-record LEVEL=<name>"; exit 1; }
	@mkdir -p $(TRACE_DIR) $(TRACE_INPUTS)
	@mkdir -p $(dir $(SAVE_FILE))
	@$(MAKE) --no-print-directory direct-boot LEVEL=$(LEVEL) SL_NAME=$(SL_NAME) \
	    DIFFICULTY=$(DIFFICULTY) TOUGH=$(TOUGH)
	@sed 's/ SRC=[0-9a-f]*//' build/u/direct/ge007.u.$(SL_NAME).z64.build \
	    > $(TRACE_INPUTS)/$(SL_NAME).spec 2>/dev/null || true
	@$(PYTHON) $(TRACE_TOOL) record \
	    --out $(TRACE_INPUTS)/$(SL_NAME).input \
	    --trace $(TRACE_DIR)/$(SL_NAME).sltrace \
	    --eeprom $(SAVE_FILE) \
	    --rom build/u/direct/ge007.u.$(SL_NAME).z64 \
	    --ticks $(RECORD_TICKS) \
	    --level $(SL_NAME)

# Record a session against the DEBUG ROM, with LIVE capture feedback.
#
# This is `trace-record` pointed at the SIGHTLINE_ROM_DEBUG image, with the
# recorder driving the mailbox as you play. Press CREATE (left of the
# DualSense touchpad - InputFrame bit 14, which never reaches the core) and the
# terminal answers immediately:
#
#   capture 3  frame 4812  HIT   prop 0x800c93f0  type 0x2f TINTED_GLASS  dist 512.4
#   capture 4  frame 5140  MISS  (crosshair on no prop; 14 props on screen)
#   capture 5  frame 5390  MISS  (not aiming - no crosshair)
#
#   make trace-record-debug LEVEL=facility SL_NAME=facility-glass
#   make trace-record-debug LEVEL=facility SL_NAME=facility-glass AIM_AID=47
#
# AIM_AID=<PROPDEF> turns on the proximity aid (47 = TINTED_GLASS): one line
# naming the nearest one and its distance, printed only when that line changes.
# Default off, because it costs a mailbox command every AID_EVERY frames and
# EVERY command perturbs the run.
#
# WHAT THIS RECORDING IS: an inspection artefact. The debug ROM is knowingly
# non-matching, and a capture press perturbs the run from roughly 110 game ticks
# later (docs/backlog.md, B-051). The recorder says so at both ends of the run
# rather than leaving it to be discovered. For pixel-fidelity or trace work,
# record again with plain `make trace-record` against the normal ROM.
#
# The state trace goes to build/trace/, NEVER to $(TRACE_DIR): trace-verify
# globs that directory and treats every .sltrace in it as a level to gate.
AIM_AID ?=
AID_EVERY ?= 120
trace-record-debug: check-python verify-rom
	@test -n "$(LEVEL)" || { echo "usage: make trace-record-debug LEVEL=<name> [SL_NAME=id] [AIM_AID=47]"; exit 1; }
	@if [ -f "$(TRACE_DIR)/$(SL_NAME).sltrace" ] && [ -z "$(FORCE)" ]; then \
	  echo "::error:: $(SL_NAME) already has a PARITY recording"; \
	  echo "  $(TRACE_DIR)/$(SL_NAME).sltrace exists, so $(TRACE_INPUTS)/$(SL_NAME).input"; \
	  echo "  is a stream the gate has measured. A debug recording would overwrite it"; \
	  echo "  with a stream made against a NON-MATCHING ROM, which silently invalidates"; \
	  echo "  every gate result keyed to that recording's content."; \
	  echo "  Give the capture its own name:"; \
	  echo "    make trace-record-debug LEVEL=$(LEVEL) SL_NAME=$(SL_NAME)-capture"; \
	  exit 1; \
	fi
	@mkdir -p $(TRACE_INPUTS) build/trace $(dir $(SAVE_FILE))
	@$(MAKE) --no-print-directory direct-boot ROM_DEBUG=1 LEVEL=$(LEVEL) \
	    SL_NAME=$(SL_NAME) DIFFICULTY=$(DIFFICULTY) TOUGH=$(TOUGH)
	@sed 's/ SRC=[0-9a-f]*//' build/u/direct/ge007.u.$(SL_NAME).dbg.z64.build \
	    > $(TRACE_INPUTS)/$(SL_NAME).spec 2>/dev/null || true
	@$(PYTHON) $(TRACE_TOOL) record \
	    --out $(TRACE_INPUTS)/$(SL_NAME).input \
	    --trace build/trace/$(SL_NAME).dbg.sltrace \
	    --eeprom $(SAVE_FILE) \
	    --rom build/u/direct/ge007.u.$(SL_NAME).dbg.z64 \
	    --map build/u/direct/ge007.u.$(SL_NAME).dbg.map \
	    --rom-debug \
	    --capture-out build/trace/$(SL_NAME)-captures \
	    --aid-every $(AID_EVERY) \
	    $(if $(AIM_AID),--aim-aid $(AIM_AID),) \
	    $(if $(SYNTH_SELECT),--synth-select $(SYNTH_SELECT),) \
	    --ticks $(RECORD_TICKS) \
	    --level $(SL_NAME)

# Record-then-replay in our own format, with a scripted pad so it needs no
# human. Everything else in Phase 0 assumes this holds: if the logged stream is
# applied one frame early, every trace we record is a recording of the wrong
# run. Each half runs in its own process - dlopen would otherwise hand replay
# the recorder's still-loaded core. usage: make trace-roundtrip [FRAMES=600]
FRAMES ?= 600
trace-roundtrip: check-python verify-rom
	@$(PYTHON) tools/trace/roundtrip.py --frames $(FRAMES) \
	    --rom $(BUILD_DIR)/ge007.$(OUTCODE).z64 \
	    --map $(BUILD_DIR)/ge007.$(OUTCODE).map

# Field-level detail at a divergent tick. This is what trace-diff points you at.
# usage: make trace-diff LEVEL=facility TICK=137
trace-diff: check-python
	@test -n "$(TICK)" || { echo "usage: make trace-diff LEVEL=<name> TICK=<n>"; exit 1; }
	@$(XVFB) $(PYTHON) $(TRACE_TOOL) detail --tick $(TICK) --window 2 \
	    --out $(TRACE_DIR)/$(LEVEL)-tick$(TICK).json
	@echo "Compare against the other build's dump with:"
	@echo "  $(PYTHON) $(TRACE_TOOL) detail-diff <a>.json <b>.json"



commonclean:
	rm -f $(APPELF) $(APPROM) $(APPBIN) $(BUILD_DIR)/ge007.$(OUTCODE).map

setupclean: commonclean
	rm -f $(SETUP_BUILD_FILES)

stanclean: commonclean
	rm -f $(STAN_BUILD_FILES)

dataclean: commonclean stanclean setupclean
	rm -f $(OBSEG_OBJECTS) $(OBSEG_RZ) $(ROMOBJECTS) $(RAMROM_OBJECTS) $(FONTOBJECTS) $(MUSIC_OBJECTS) $(IMAGE_OBJS) $(MUSIC_RZ_FILES)
	rm -f $(BUILD_DIR)/imagelist.csv

libultraclean: commonclean
	rm -f $(ULTRAOBJECTS)

codeclean: commonclean libultraclean
	rm -f $(HEADEROBJECTS) $(BOOTOBJECTS) $(CODEOBJECTS) $(GAMEOBJECTS) $(RZOBJECTS) $(RSPOBJECTS)

clean: codeclean dataclean
	@echo "\nAll Code and Asset Binaries Cleared! Make will Re-Build these next time.\n"

nuke: clean
	scripts/make/clean_nuke.sh "$(ALLOWED_COUNTRYCODE)" "$(BUILD_DIR_BASE)"

help:
	@echo "mmakefile help"
	@echo ""
	@echo "  supported targets:"
	@echo ""
	@echo "    all                            Build all (default)"
	@echo "    clean                          Delete all known build artifacts"
	@echo "    nuke                           Delete all files explicitly listed in Makefile (same as make clean),"
	@echo "                                    all build output for all versions, any .bin file in assets folders,"
	@echo "                                    and asp/rsp bin."
	@echo "    dataclean                      Delete only asset build artifacts"
	@echo "    codeclean                      Delete only code (asm, .c) build artifacts"
	@echo "    libultraclean                  Delete only code (asm, .c) build artifacts "
	@echo "                                    from Rare's libultra files"
	@echo "    stanclean                      Delete only stan build artifacts"
	@echo "    setupclean                     Delete only setup build artifacts"
	@echo "    cmdbuidler                     BuildAI Commands"
	@echo "    context [file]                 BuildContext File from [file]"
	@echo "                                    eg make context src/game/chrai.c"
	@echo "    test                            Re-Run Data Verification "
	@echo ""
	@echo ""
	@echo "  options:"
	@echo ""
	@echo "    VERSION=v                       Region version. (US is default)"
	@echo "                                    Supported values: ${ALLOWED_VERSIONS}\n"

include include/make/cmd.make


test: checksum


ifneq ($(filter-out context,$(MAKECMDGOALS)),)
 CONTEXTFILE := $(filter-out context ,$(MAKECMDGOALS))
else
 CONTEXTFILE := build/ctx.c
endif
context:
	@clear
	@echo Building Context File [ctx.h] from $(CONTEXTFILE)
	@echo "#define TRUE 1" > build/ctx.h
	@echo "#define FALSE 0" >> build/ctx.h
ifeq ($(CONTEXTFILE),build/ctx.c)
	@echo "#include <bondtypes.h>" > build/ctx.c
endif
	@sed -n -E ':x /\\$$/ { N; s/\\\n//g ; bx };''/(^\s*#define)|(\\$$)/p; /(\\$$)/p;' src/bondconstants.h src/bondtypes.h $(CONTEXTFILE) >> build/ctx.h
	@$(CC) -c $(CFLAGS) $(CONTEXTFILE) -E > build/ctx2.h 2> /dev/null || (rm build/ctx2.h && exit 1)
	@sed -E '/^\s*$$/d' build/ctx2.h >> build/ctx.h
	@rm build/ctx.c build/ctx2.h || exit 0
	@echo You can find it in Build [build/ctx.h].

extractassets: extract_u extract_e extract_j convert_props convert_chrs convert_guns

forceextractassets: force_extract_u force_extract_e force_extract_j convert_props convert_chrs convert_guns

extract_u:
	@if [ ! -f assets/obseg/ob__ob_end.seg ]; then \
		echo "Extracting assets for u..."; \
		if [ -f baserom.u.z64 ]; then \
			scripts/extract_baserom.u.sh; \
		else \
			echo "Error: baserom.u.z64 not found."; \
		fi \
	else \
		echo "Assets for u already extracted."; \
	fi

force_extract_u:
	@echo "Force extracting assets for u..."; \
	if [ -f baserom.u.z64 ]; then \
		scripts/extract_baserom.u.sh; \
	else \
		echo "Error: baserom.u.z64 not found."; \
	fi

extract_e:
	@if [ ! -f assets/obseg/text/e/LwaxP.bin ]; then \
		echo "Extracting assets for e..."; \
		if [ -f baserom.e.z64 ]; then \
			scripts/extract_diff.e.sh; \
		else \
			echo "Error: baserom.e.z64 not found."; \
		fi \
	else \
		echo "Assets for e already extracted."; \
	fi

force_extract_e:
	@echo "Force extracting assets for e..."; \
	if [ -f baserom.e.z64 ]; then \
		scripts/extract_diff.e.sh; \
	else \
		echo "Error: baserom.e.z64 not found."; \
	fi

extract_j:
	@if [ ! -f assets/obseg/text/j/LstatJ.bin ]; then \
		echo "Extracting assets for j..."; \
		if [ -f baserom.j.z64 ]; then \
			scripts/extract_diff.j.sh; \
		else \
			echo "Error: baserom.j.z64 not found."; \
		fi \
	else \
		echo "Assets for j already extracted."; \
	fi

force_extract_j:
	@echo "Force extracting assets for j..."; \
	if [ -f baserom.j.z64 ]; then \
		scripts/extract_diff.j.sh; \
	else \
		echo "Error: baserom.j.z64 not found."; \
	fi

extract_rsp:
	@if [ ! -f build/u/rsp/rspboot.bin ]; then \
		echo "Extracting rsp assets..."; \
		if [ -f baserom.u.z64 ]; then \
			scripts/extract_asp_gsp_rsp.sh; \
		else \
			echo "Error: baserom.u.z64 not found."; \
		fi \
	else \
		echo "RSP assets for already extracted."; \
	fi

convert_props:
	@echo "Converting prop binaries to Model.c..."
	@bin_count=$$(ls assets/obseg/prop/P*Z.bin 2>/dev/null | wc -l); \
	if [ $$bin_count -gt 0 ]; then \
		echo "Found $$bin_count prop binaries to convert..."; \
		$(PYTHON) scripts/generate_prop_model_c.py --force --cleanup || true; \
	else \
		c_count=$$(find assets/obseg/prop -maxdepth 2 -name "Model.c" 2>/dev/null | wc -l); \
		if [ $$c_count -gt 0 ]; then \
			echo "Props already converted ($$c_count Model.c files found)."; \
		else \
			echo "No prop binaries found to convert."; \
		fi \
	fi

convert_chrs:
	@echo "Converting chr binaries to Model.c..."
	@bin_count=$$(ls assets/obseg/chr/C*Z.bin 2>/dev/null | wc -l); \
	if [ $$bin_count -gt 0 ]; then \
		echo "Found $$bin_count chr binaries to convert..."; \
		$(PYTHON) scripts/generate_chr_c.py --force --cleanup || true; \
	else \
		c_count=$$(find assets/obseg/chr -maxdepth 2 -name "Model.c" 2>/dev/null | wc -l); \
		if [ $$c_count -gt 0 ]; then \
			echo "Chrs already converted ($$c_count Model.c files found)."; \
		else \
			echo "No chr binaries found to convert."; \
		fi \
	fi

convert_guns:
	@echo "Converting gun binaries to Model.c..."
	@bin_count=$$(ls assets/obseg/gun/G*Z.bin 2>/dev/null | wc -l); \
	if [ $$bin_count -gt 0 ]; then \
		echo "Found $$bin_count gun binaries to convert..."; \
		$(PYTHON) scripts/generate_gun_c.py --force --cleanup || true; \
	else \
		c_count=$$(find assets/obseg/gun -maxdepth 2 -name "Model.c" 2>/dev/null | wc -l); \
		if [ $$c_count -gt 0 ]; then \
			echo "Guns already converted ($$c_count Model.c files found)."; \
		else \
			echo "No gun binaries found to convert."; \
		fi \
	fi

textures: tools/mktex/build/tex2png
	@echo "Processing textures..."
	mkdir -p assets/images/out
	$(foreach x,$(IMAGE_BINS),tools/mktex/build/tex2png $(x) assets/images/out ${\n})


tools/mktex/build/tex2png:
	@if [ ! -f tools/mktex/build/tex2png ]; then \
		echo "Building tex2png..."; \
		cd tools/mktex && $(MAKE); \
	fi
