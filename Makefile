# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris <goabonga@pm.me>

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude
CPPFLAGS += -D_GNU_SOURCE
PREFIX  ?= /usr/local
SANITIZE ?= address,undefined
SOURCES := $(wildcard src/*.c include/*.h tests/*.c)
BUILD   := build

.PHONY: all test format format-check lint clean install

all: $(BUILD)/gpd-hotkeysd

$(BUILD)/gpd-hotkeysd: src/gpd-hotkeysd.c include/version.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ src/gpd-hotkeysd.c

$(BUILD):
	mkdir -p $@

# Unit tests of the pure logic, built with the address and undefined
# behaviour sanitizers.
# Disable them with `make test SANITIZE=` where the platform rejects them.
test: $(BUILD)/test_logic
	$(BUILD)/test_logic

$(BUILD)/test_logic: tests/test_logic.c src/logic.c include/logic.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -g $(if $(SANITIZE),-fsanitize=$(SANITIZE) -fno-sanitize-recover=all) \
		$(LDFLAGS) -o $@ tests/test_logic.c src/logic.c -lm

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
