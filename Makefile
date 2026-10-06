# ==============
# Default Target
# ==============

.DEFAULT_GOAL := all
.PHONY: all
all: build

CC      := xc16-gcc
OBJCOPY := xc16-objcopy
BIN2HEX := xc16-bin2hex
#SILENCE := 2> >(grep -v -e "^XCLM: Failed to elevate" -e "^Info: Loading file" -e "^[[:space:]]*$$" >&2)

# ===================
# Directory Constants
# ===================

SDK_ROOT     := $(patsubst %/,%,$(dir $(realpath $(lastword $(MAKEFILE_LIST)))))
SRC_TREE     := $(patsubst %/,%,$(dir $(realpath $(firstword $(MAKEFILE_LIST)))))
XC16_ROOT    := $(patsubst %/,%,$(dir $(shell xc16-gcc -print-prog-name=cc1)))/..
OUTPUT_DIR   := $(CURDIR)
PLATFORM_DIR := $(SDK_ROOT)/platform/$(PLATFORM)

BUILD_DIR    := $(OUTPUT_DIR)/build
OBJ_DIR      := $(BUILD_DIR)/objs
DEP_DIR      := $(BUILD_DIR)/deps
ELF          := $(BUILD_DIR)/firmware.elf
HEX          := $(BUILD_DIR)/firmware.hex
MAP          := $(BUILD_DIR)/firmware.map
MDB          := $(BUILD_DIR)/mdb.txt

# ================
# Build Parameters
# ================

INCLUDE      += -isystem $(XC16_ROOT)/support/generic/h \
                -isystem $(SDK_ROOT)/include
inc-y        ?=

DEFINE       +=
SOURCES      +=
src-y        ?=

MODULES      +=
mod-y        ?=

CFLAGS += -W -Wall -Wextra -Wundef -Wshadow -Wdouble-promotion \
          -fno-common \
          -MD -MP -mcpu=24F16KA101 -std=c99 -O2 -mpa

LDFLAGS += -Xlinker -Map \
           -Xlinker $(MAP)

# ========
# Includes
# ========

-include $(PLATFORM_DIR)/Makefile

# ============
# Object Files
# ============

.SECONDEXPANSION:
SOURCES      += $(src-y)
OBJS         = $(SOURCES:%.c=$(OBJ_DIR)/%.o)
DEPS         = $(SOURCES:%.c=$(DEP_DIR)/%.d)
INCLUDE      += $(inc-y)
MODULES      += $(mod-y)

# =======
# Targets
# =======

-include $(DEPS)

# Build object files and dependency files (.o and .d)
$(OBJ_DIR)/%.o: %.c
	@printf '\tCC\t%s\n' $<
	@mkdir -p $(dir $@) $(dir $(DEP_DIR)/$*)
	@$(CC) $(CFLAGS) $(INCLUDE) $(DEFINE) -c $< -o $@ -MF $(DEP_DIR)/$*.d $(SILENCE)

# Build flashable firmware
$(ELF): $$(OBJS)
	@printf '\tLD\t%s\n' $@
	@$(CC) $(OBJS) $(CFLAGS) $(LDFLAGS) -o $@ $(SILENCE)

$(HEX): $(ELF)
	@printf '\tBIN2HEX\t%s\n' $@ $(SILENCE)
	@$(BIN2HEX) $<

# Other Helper Targets

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

.PHONY: build
build: $(HEX)

.PHONY: flash
flash: $(HEX)
	@printf '\tFLASH\t%s\n'
	@ipecmd -P$(FLASH_PART) -TP$(FLASH_TOOL) -F$(HEX) -M -OL

$(MDB): $(BUILD_DIR)
	@printf '\tMDB\t%s\n' $@
	@echo "$$MDB_CONTENTS" > $(MDB)

.PHONY: debug
debug: $(ELF) $(MDB)
	@printf '\tDEBUG\t%s\n'
	@mdb $(MDB)

.PHONY: clean
clean:
	@printf '\tRM\t%s\n' build/
	@rm -rf $(BUILD_DIR)

.PHONY: compile_commands.json
compile_commands.json:
	@bear -- $(MAKE) -B build
	@cp $(CURDIR)/compile_commands.json $(SDK_ROOT)/
