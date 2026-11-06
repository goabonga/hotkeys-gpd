# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris <goabonga@pm.me>

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude
PREFIX  ?= /usr/local
SANITIZE ?= address,undefined
SOURCES := $(wildcard src/*.c include/*.h tests/*.c)
BUILD   := build

CPPFLAGS += -D_GNU_SOURCE $(shell pkg-config --cflags libevdev alsa)
LDLIBS  += $(shell pkg-config --libs libevdev alsa) -lm
OBJS    := $(BUILD)/gpd-hotkeysd.o $(BUILD)/logic.o
SAN     := $(if $(SANITIZE),-fsanitize=$(SANITIZE) -fno-sanitize-recover=all)

.PHONY: all test format format-check lint clean install

all: $(BUILD)/gpd-hotkeysd

$(BUILD)/gpd-hotkeysd: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c include/logic.h include/version.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $@

# Unit tests, built with the address and undefined behaviour sanitizers,
# then the daemon tests on a fake backlight.
# Disable the sanitizers with `make test SANITIZE=` where the platform
# rejects them.
test: $(BUILD)/test_logic $(BUILD)/gpd-hotkeysd
	$(BUILD)/test_logic
	tests/tests.sh $(BUILD)/gpd-hotkeysd

$(BUILD)/test_logic: tests/test_logic.c src/logic.c include/logic.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -g $(SAN) $(LDFLAGS) -o $@ \
		tests/test_logic.c src/logic.c -lm

format:
	clang-format -i $(SOURCES)

format-check:
	clang-format --dry-run -Werror $(SOURCES)

lint:
	clang-tidy --quiet $(filter %.c,$(SOURCES)) -- $(CPPFLAGS) -std=c11 -Iinclude

install: all
	install -Dm755 $(BUILD)/gpd-hotkeysd $(DESTDIR)$(PREFIX)/bin/gpd-hotkeysd

clean:
	rm -rf $(BUILD)
