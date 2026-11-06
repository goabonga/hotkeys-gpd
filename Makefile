# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris <goabonga@pm.me>

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude
CPPFLAGS += -D_GNU_SOURCE
PREFIX  ?= /usr/local
BUILD   := build

.PHONY: all test clean install

all: $(BUILD)/gpd-hotkeysd

$(BUILD)/gpd-hotkeysd: src/gpd-hotkeysd.c include/version.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ src/gpd-hotkeysd.c

$(BUILD):
	mkdir -p $@

# Unit tests of the pure logic, built with the address and undefined
# behaviour sanitizers.
test: $(BUILD)/test_logic
	$(BUILD)/test_logic

$(BUILD)/test_logic: tests/test_logic.c src/logic.c include/logic.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all \
		$(LDFLAGS) -o $@ tests/test_logic.c src/logic.c -lm

install: all
	install -Dm755 $(BUILD)/gpd-hotkeysd $(DESTDIR)$(PREFIX)/bin/gpd-hotkeysd

clean:
	rm -rf $(BUILD)
