/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris <goabonga@pm.me> */

/* Pure logic of gpd-hotkeysd: no hardware I/O, testable as a regular user. */

#ifndef LOGIC_H
#define LOGIC_H

#include <stdbool.h>

/* Handled keys (internal index). */
enum hotkey {
	HK_BRIGHTNESS_UP,
	HK_BRIGHTNESS_DOWN,
	HK_VOLUME_UP,
	HK_VOLUME_DOWN,
	HK_MUTE,
	HK_COUNT
};

/*
 * New raw brightness after one step of pct % of max, clamped to [min, max].
 * Below 10 % of max the step is 1 % (at least 1) and a step crossing the
 * 10 % threshold stops on it, in both directions. An invalid max (< 1)
 * leaves cur unchanged.
 */
int brightness_next(int cur, int max, int pct, int min, bool up);

/* Rounded percentage of v within [min, max]; 100 when the range is empty. */
int percent(long v, long min, long max);

/* New volume (1/100 dB) after one step, clamped to [min, max]. */
long volume_next(long cur, long step, long min, long max, bool up);

/*
 * Duplicate filtering (the same key reported by several devices) and
 * autorepeat rate limiting.
 */
struct dedup {
	struct {
		int dev;
		long press_ms;
		long action_ms;
		bool seen;
	} t[HK_COUNT];
};

/*
 * value: 0 release, 1 press, 2 repeat. Returns true if an action is due: a
 * press not duplicating another device's press less than dedup_ms earlier,
 * or a repeat from the pressing device at least repeat_ms after the last
 * action (never for mute).
 */
bool dedup_accept(struct dedup *d, enum hotkey k, int dev, int value,
		  long now_ms, int dedup_ms, int repeat_ms);

/* KEY_* name (case-insensitive, "KEY_" optional) to key; -1 if unknown. */
int hotkey_by_name(const char *name);

extern const char *const hotkey_names[HK_COUNT];

#endif /* LOGIC_H */
